/* JSON-RPC 2.0 HTTP/BLE/MQTT Server for ESP32-C3 */

#include "ble_gatt_server.h"
#include "buzzer.h"
#include "config.h"
#include "dht11.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_control.h"
#include "http_server.h"
#include "max7219.h"
#include "mqtt_manager.h"
#include "nvs_flash.h"
#include "store.h"
#include "wifi_manager.h"

#include <stdio.h>

static const char *TAG = "MAIN";

void app_main(void) {
    /* Initialize NVS */
    init_nvs();

    /* Initialize GPIO */
    // gpio_control_init();

    /* Initialize RGB LED */
    // rgb_led_init();

    /* Initialize DHT11 temperature & humidity sensor */
    dht11_init();

    /* Initialize MAX7219 8x8 LED matrix (with demo) */
    max7219_init();

    /* Initialize buzzer and run demo (4x on/off every 5s) */
    // buzzer_init();
    // buzzer_demo();

    /* Initialize WiFi (must be before BLE to allow BLE provisioning if needed) */
    initialise_wifi();

    /* Start HTTP Server */
    http_server_start();

    /* Start MQTT client (runs after WiFi is up) */
    ESP_LOGI(TAG, "Starting MQTT client...");
    if (mqtt_manager_start() == ESP_OK) {
        ESP_LOGI(TAG, "MQTT client started (broker=%s:%d, heartbeat=%ds)", MQTT_BROKER_HOST, MQTT_BROKER_PORT,
                 MQTT_HEARTBEAT_INTERVAL_S);
    } else {
        ESP_LOGE(TAG, "Failed to start MQTT client");
    }

    /* Start BLE GATT server (JSON-RPC over NUS-compatible service) */
    ESP_LOGI(TAG, "Starting BLE GATT server...");
    if (ble_gatt_server_init() == ESP_OK) {
        ESP_LOGI(TAG, "BLE GATT server started (device name: %s)", DEVICE_NAME);
    } else {
        ESP_LOGE(TAG, "Failed to start BLE GATT server");
    }
}
