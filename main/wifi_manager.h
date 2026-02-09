#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Initialize WiFi provisioning or connect if already provisioned
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Check if device is provisioned
 * @return true if provisioned, false otherwise
 */
bool wifi_manager_is_provisioned(void);

/**
 * @brief Get the WiFi event group handle
 * @return EventGroupHandle_t
 */
void *wifi_manager_get_event_group(void);

/**
 * @brief Get the configured WiFi SSID
 * @return const char* pointer to SSID string
 */
const char *wifi_manager_get_ssid(void);

/**
 * @brief Reset WiFi provisioning (for testing/debugging)
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_reset_provisioning(void);

#endif // WIFI_MANAGER_H
