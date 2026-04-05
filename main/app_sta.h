
#ifndef APP_STA_H
#define APP_STA_H

#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"

/**
 * Initialize the ESP8266 as an access point with configurations from the Kconfig.projbuild file.
 * 
 * - CONFIG_ESP_WIFI_SSID : name of access point
 * - CONFIG_ESP_WIFI_PASSWORD : password for access point
 * - CONFIG_ESP_MAX_STA_CONN : maximum number of stations connected
 */
void wifi_init_sta();

#endif
