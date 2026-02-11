#ifndef BLE_SERVER_H
#define BLE_SERVER_H

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialize and start the BLE GATT server for JSON-RPC
 *
 * This function initializes Bluetooth controller and Bluedroid stack,
 * creates a GATT service with characteristics for JSON-RPC communication,
 * and starts BLE advertising.
 *
 * Service UUID: 5f6d4f53-5f52-5043-5f53-56435f49445f  (Shelly-style)
 * Characteristics:
 *   - TX_CTL (Write): Client sends RPC requests
 *   - RX_CTL (Read/Notify): Server sends RPC responses
 *   - DATA (Read/Write): For larger payload transfers
 *
 * Frame Protocol: 2-byte length (little-endian) + JSON payload
 *
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t ble_server_init(void);

/**
 * @brief Get BLE connection status
 *
 * @return true if a client is connected, false otherwise
 */
bool ble_server_is_connected(void);

/**
 * @brief Get current MTU size
 *
 * @return Current negotiated MTU size (default 23, max 512)
 */
uint16_t ble_server_get_mtu(void);

#endif // BLE_SERVER_H
