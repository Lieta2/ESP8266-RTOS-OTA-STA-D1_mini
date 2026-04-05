/* MQTT (over TCP) Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdbool.h>
#include "app_mqtt.h"
#include "app_gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"


#ifndef MIN
   #define MIN(x,y) ((x)<(y)?(x):(y))
#endif

#ifndef MAX
   #define MAX(x,y) ((x)>(y)?(x):(y))
#endif


static const char *TAG = "MQTT";
static esp_mqtt_client_handle_t client;
static char device_id[32];
static char lwt_topic[64];
static bool mqtt_connected = false;

void publish_switch_discovery(int gpio)
{
    char topic[128];
    char payload[600];

    const char *pin_name = gpio_to_name(gpio);
    if (!pin_name) {
        ESP_LOGE(TAG, "Invalid GPIO mapping: %d", gpio);
        return;
    }

    snprintf(topic, sizeof(topic),
        "homeassistant/switch/%s/%s/config",
        device_id, pin_name);

    int len = snprintf(payload, sizeof(payload),
	        "{"
	        "\"name\":\"%s\","
                "\"state_topic\":\"%s/gpio/%s/state\","
	        "\"command_topic\":\"%s/gpio/%s/set\","
	        "\"unique_id\":\"%s_%s\","
	        "\"payload_on\":\"1\","
	        "\"payload_off\":\"0\","
	        "\"availability_topic\":\"%s/status\","
	        "\"payload_available\":\"online\","
	        "\"payload_not_available\":\"offline\","
	        "\"device\":{"
	            "\"identifiers\":[\"%s\"],"
	            "\"name\":\"%s\""
	        "}"
	        "}",
	        pin_name,
	        device_id, pin_name,
	        device_id, pin_name,
	        device_id, pin_name,
	        device_id,
	        device_id,
	        device_id
	    );
    if (len >= sizeof(payload)) {
        ESP_LOGE(TAG, "Payload truncated");
    }

    if (client) {
        esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);
    }
}

void publish_binary_sensor_discovery(int gpio)
{
    char topic[128];
    char payload[600];

    const char *pin_name = gpio_to_name(gpio);
    if (!pin_name) {
        ESP_LOGE(TAG, "Invalid GPIO mapping: %d", gpio);
        return;
    }

    snprintf(topic, sizeof(topic),
        "homeassistant/binary_sensor/%s/%s/config",
        device_id, pin_name);

    int len = snprintf(payload, sizeof(payload),
	        "{"
	        "\"name\":\"%s\","
	        "\"state_topic\":\"%s/gpio/%s/state\","
	        "\"unique_id\":\"%s_%s\","
	        "\"payload_on\":\"0\","
	        "\"payload_off\":\"1\","
	        "\"availability_topic\":\"%s/status\","
	        "\"payload_available\":\"online\","
	        "\"payload_not_available\":\"offline\","
	        "\"device\":{"
	            "\"identifiers\":[\"%s\"],"
	            "\"name\":\"%s\""
	        "}"
	        "}",
	        pin_name,
	        device_id, pin_name,
	        device_id, pin_name,
	        device_id,
	        device_id,
	        device_id
	    );
    if (len >= sizeof(payload)) {
        ESP_LOGE(TAG, "Payload truncated");
    }

    if (client) {
        esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);
    }
}

void save_gpio_state(int gpio, int level)
{
    nvs_handle_t nvs;
    if (nvs_open("gpio", NVS_READWRITE, &nvs) == ESP_OK) {
        char key[16];
        snprintf(key, sizeof(key), "g%d", gpio);

        nvs_set_u8(nvs, key, level);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void mqtt_event_handler(void *handler_args,
                              esp_event_base_t base,
                              int32_t event_id,
                              void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    if (!event) {
        ESP_LOGE(TAG, "MQTT event is NULL");
        return;
    }

    switch (event->event_id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Connected");
        mqtt_connected = true;

        // 🟢 Publish ONLINE state
        esp_mqtt_client_publish(
        	client,
            lwt_topic,
            "online",
            0,
            1,
            1
        );

        // subscribe to output control
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            char topic[128];
            snprintf(topic, sizeof(topic),
                     "%s/gpio/%s/set", device_id, gpio_to_name(output_pins[i]));

            esp_mqtt_client_subscribe(client, topic, 1);
        }
        // 🟢 Send Home Assistant discovery
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            publish_switch_discovery(output_pins[i]);
        }
        for (int i = 0; i < NUM_INPUTS; i++) {
            publish_binary_sensor_discovery(input_pins[i]);
        }
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            gpio_num_t pin = output_pins[i];
            publish_pin_state(gpio_to_name(pin), gpio_get_level(pin));
        }
        for (int i = 0; i < NUM_INPUTS; i++) {
            gpio_num_t pin = input_pins[i];
            publish_pin_state(gpio_to_name(pin), gpio_get_level(pin));
        }

        break;

    case MQTT_EVENT_DISCONNECTED:
	mqtt_connected = false;
        ESP_LOGI(TAG, "Disconnected");
        break;

    case MQTT_EVENT_PUBLISHED:
        // optional debug
        break;

    case MQTT_EVENT_DATA: {
        char topic[128] = {0};
        char data[16] = {0};

        size_t len = MIN(event->topic_len, sizeof(topic) - 1);
        memcpy(topic, event->topic, len);
        topic[len] = '\0';

        len = MIN(event->data_len, sizeof(data) - 1);
        memcpy(data, event->data, len);
        data[len] = '\0';

        ESP_LOGI(TAG, "RX: %s -> %s", topic, data);

        // topic: ESP8266_xxx/gpio/D1/set

        char *pin = strstr(topic, "/gpio/");
        if (pin) {
            pin += 6; // skip "/gpio/"
            char *end = strstr(pin, "/set");
            if (end) {
                *end = '\0';
                gpio_num_t gpio = name_to_gpio(pin);
                if (gpio == GPIO_NUM_MAX) {
                    ESP_LOGE(TAG, "Invalid GPIO: %s", pin);
                    return;
                }
                int level = (
                    strcmp(data, "1") == 0 ||
                    strcasecmp(data, "on") == 0 ||
                    strcasecmp(data, "true") == 0
                ) ? 1 : 0;

                gpio_set_level(gpio, level);
                save_gpio_state(gpio, level);

                // publish new state
                publish_pin_state(pin, level);
            }
        }
        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Error");
        break;

    default:
        break;
    }
}

void init_device_id()
{
    uint8_t mac[6];

    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));

    snprintf(device_id, sizeof(device_id),
             "ESP8266_%02X%02X%02X",
             mac[3], mac[4], mac[5]);
}

void publish_pin_state(const char *pin, int level)
{
    char topic[128];
    char payload[8];

    ESP_LOGI(TAG, "%s: %d", pin, level);
    snprintf(topic, sizeof(topic),
             "%s/gpio/%s/state", device_id, pin);

    snprintf(payload, sizeof(payload), "%d", level);

    if (mqtt_connected)
        esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);
}

void mqtt_start(void)
{
#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.uri, "FROM_STDIN") == 0) {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128) {
            int c = fgetc(stdin);
            if (c == '\n') {
                line[count] = '\0';
                break;
            } else if (c > 0 && c < 127) {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.uri = line;
        printf("Broker url: %s\n", line);
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    esp_log_level_set(TAG, ESP_LOG_INFO);
    //esp_log_level_set(TAG, ESP_LOG_VERBOSE);
    //esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    //esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    //esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    //esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    init_device_id();

    snprintf(lwt_topic, sizeof(lwt_topic),
             "%s/status", device_id);

    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_BROKER_URL,
        .client_id = device_id,
        .username = CONFIG_BROKER_USER,
        .password = CONFIG_BROKER_PASSWORD,
        .lwt_topic = lwt_topic,
        .lwt_msg = "offline",
        .lwt_qos = 1,
        .lwt_retain = 1,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);

    esp_mqtt_client_register_event(
        client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL
    );

    esp_mqtt_client_start(client);
}

