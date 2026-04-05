
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "app_sta.h"
#include "app_http.h"
#include "app_ota.h"
#include "app_mqtt.h"
#include "app_gpio.h"

esp_ota_firm_t ota_firm;

static const char *TAG = "MAIN";

void app_main()
{
    ESP_LOGI(TAG, "Startup..");
    ESP_LOGI(TAG, "Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());

    ESP_ERROR_CHECK(nvs_flash_init());
    
    // Create event loop to handle WiFi and HTTP events
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    // Initialize OTA-update
    ESP_ERROR_CHECK(init_ota(&ota_firm));

    // Initialize HTTP server for the webpage to upload updates
    init_http();

    gpio_init_outputs();
    gpio_restore_states();
    gpio_init_inputs();

    xTaskCreate(gpio_event_task, "gpio_event", 4096, NULL, 5, NULL);
}
