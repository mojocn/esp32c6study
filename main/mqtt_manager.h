#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include "esp_err.h"

/**
 * @brief Initialize and start the MQTT client.
 *
 * Connects to the broker defined by MQTT_BROKER_URI in config.h.
 * Subscribes to <DEVICE_NAME>/rpc for JSON-RPC requests.
 * Publishes a heartbeat to <DEVICE_NAME>/status every MQTT_HEARTBEAT_INTERVAL_S seconds.
 *
 * @return ESP_OK on success
 */
esp_err_t mqtt_manager_start(void);

/**
 * @brief Publish a message on a given MQTT topic.
 *
 * @param topic  Full topic string
 * @param data   Payload string (null-terminated)
 * @return ESP_OK on success, ESP_FAIL if not connected
 */
esp_err_t mqtt_manager_publish(const char *topic, const char *data);

#endif // MQTT_MANAGER_H
