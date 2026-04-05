
#ifndef APP_MQTT_H
#define APP_MQTT_H

/**
 * Initialize the ESP8266 as an access point with configurations from the Kconfig.projbuild file.
 * 
 * - CONFIG_ESP_WIFI_SSID : name of access point
 * - CONFIG_ESP_WIFI_PASSWORD : password for access point
 * - CONFIG_ESP_MAX_STA_CONN : maximum number of stations connected
 */
void mqtt_start();
void publish_pin_state(const char *pin, int level);

#endif
