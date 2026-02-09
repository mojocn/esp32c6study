#ifndef BLE_SERVER_H
#define BLE_SERVER_H

#include "esp_err.h"

/**
 * @brief Initialize BLE server with JSON-RPC support
 * @return ESP_OK on success
 */
esp_err_t ble_server_init(void);

#endif // BLE_SERVER_H
