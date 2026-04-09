#include <string.h>
#include "app_gpio.h"
#include "app_mqtt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

// Outputs
const gpio_num_t output_pins[] = {
    GPIO_NUM_16, // D0
    GPIO_NUM_5,  // D1
    GPIO_NUM_4,  // D2
    GPIO_NUM_0,  // D3, boot strap pin, MUST be HIGH at boot
    GPIO_NUM_2   // D4, boot strap pin, MUST be HIGH at boot
};

// Inputs
const gpio_num_t input_pins[] = {
    GPIO_NUM_14, // D5
    GPIO_NUM_12, // D6
    GPIO_NUM_13, // D7
    // D8, there is external 12k pull-down resistor on this pin
    // boot strap pin, MUST be LOW at boot
    GPIO_NUM_15
};

typedef struct {
    gpio_num_t pin;
    const char *name;
} pin_map_t;

const pin_map_t pin_map[] = {
    {GPIO_NUM_16, "D0"},
    {GPIO_NUM_5,  "D1"},
    {GPIO_NUM_4,  "D2"},
    {GPIO_NUM_0,  "D3"},
    {GPIO_NUM_2,  "D4"},
    {GPIO_NUM_14, "D5"},
    {GPIO_NUM_12, "D6"},
    {GPIO_NUM_13, "D7"},
    {GPIO_NUM_15, "D8"},
};

#define DEBOUNCE_MS 50

typedef struct {
    int stable_state;
    int last_read;
    TickType_t last_change_time;
} gpio_state_t;

static QueueHandle_t gpio_evt_queue;


const char* gpio_to_name(gpio_num_t pin)
{
    for (int i = 0; i < sizeof(pin_map)/sizeof(pin_map[0]); i++) {
        if (pin_map[i].pin == pin)
            return pin_map[i].name;
    }
    return NULL;
}

gpio_num_t name_to_gpio(const char *name)
{
    if (!name) {
        return GPIO_NUM_MAX;
    }

    for (int i = 0; i < sizeof(pin_map)/sizeof(pin_map[0]); i++) {
        if (strcmp(pin_map[i].name, name) == 0) {
            return pin_map[i].pin;
        }
    }

    return GPIO_NUM_MAX; // invalid
}

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    gpio_num_t gpio_num = (gpio_num_t)(uintptr_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (xQueueSendFromISR(gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken) != pdTRUE) {
        // optional: increment drop counter
    }

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void gpio_init_outputs(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 0,
        .pull_up_en = 0,
        .pull_down_en = 0
    };

    for (int i = 0; i < NUM_OUTPUTS; i++) {
        io_conf.pin_bit_mask = (1ULL << output_pins[i]);
        gpio_config(&io_conf);
        // Special handling for bootstrap pins
        if (output_pins[i] == GPIO_NUM_0 || output_pins[i] == GPIO_NUM_2) {
            gpio_set_level(output_pins[i], 1); // SAFE default
        } else {
            gpio_set_level(output_pins[i], 0);
        }
    }
}

static int pending_state[GPIO_NUM_MAX];
static int stable_state[GPIO_NUM_MAX];
static int64_t last_change_time[GPIO_NUM_MAX];

void gpio_init_inputs(void)
{
    gpio_evt_queue = xQueueCreate(16, sizeof(gpio_num_t));
    assert(gpio_evt_queue);

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 0,
        .pull_up_en = 1,
        .pull_down_en = 0
    };

    for (int i = 0; i < NUM_INPUTS - 1; i++) {
        gpio_num_t pin = input_pins[i];
        io_conf.pin_bit_mask = (1ULL << pin);
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        stable_state[pin] = pending_state[pin] = gpio_get_level(pin);
    }

    //special handling of GPIO_15
    io_conf.pull_up_en = 0;
    io_conf.pull_down_en = 1;
    io_conf.pin_bit_mask = (1ULL << GPIO_NUM_15);
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    stable_state[GPIO_NUM_15] = pending_state[GPIO_NUM_15] = gpio_get_level(GPIO_NUM_15);

    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    for (int i = 0; i < NUM_INPUTS; i++) {
        gpio_num_t pin = input_pins[i];
        ESP_ERROR_CHECK(gpio_isr_handler_add(pin, gpio_isr_handler, (void*)(uintptr_t)pin));
        ESP_ERROR_CHECK(gpio_set_intr_type(input_pins[i], GPIO_INTR_ANYEDGE));
    }
}

void gpio_restore_states(void)
{
    nvs_handle_t nvs;

    if (nvs_open("gpio", NVS_READONLY, &nvs) != ESP_OK)
        return;

    for (int i = 0; i < NUM_OUTPUTS; i++) {
        uint8_t val;
        char key[16];

        snprintf(key, sizeof(key), "g%d", output_pins[i]);

        if (nvs_get_u8(nvs, key, &val) == ESP_OK) {
            gpio_set_level(output_pins[i], val);
        }
    }

    nvs_close(nvs);
}


void gpio_event_task(void *arg)
{
    gpio_num_t io_num;

    bool pending_change = false;
    while (1) {
        // drain queue quickly
        while (xQueueReceive(gpio_evt_queue, &io_num, pending_change ? (10 / portTICK_PERIOD_MS) : portMAX_DELAY)) {
            pending_state[io_num] = gpio_get_level(io_num);
            last_change_time[io_num] = esp_timer_get_time() / 1000;
        }

        int64_t now = esp_timer_get_time() / 1000;
        pending_change = false;

        for (int i = 0; i < NUM_INPUTS; i++) {
            gpio_num_t io = input_pins[i];
            if (pending_state[io] != stable_state[io]) {
                pending_change = true;
                if ((now - last_change_time[io]) >= DEBOUNCE_MS) {

                    stable_state[io] = pending_state[io];

                    publish_pin_state(gpio_to_name(io), stable_state[io]);
                }
            }
        }
    }
}
