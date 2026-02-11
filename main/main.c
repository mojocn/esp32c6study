/* JSON-RPC 2.0 HTTP/BLE Server for ESP32-C6 */

#include "ble_server.h"
#include "config.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_control.h"
#include "http_server.h"
#include "nvs_flash.h"
#include "store.h"
#include "wifi_manager.h"

#include <stdio.h>

static const char *TAG = "MAIN";

void app_main(void) {
    /* Initialize NVS */
    init_nvs();

    /* Initialize GPIO */
    gpio_control_init();

    /* Initialize RGB LED */
    rgb_led_init();

    /* Initialize WiFi (must be before BLE to allow BLE provisioning if needed) */
    initialise_wifi();

    /* Initialize BLE GATT Server for JSON-RPC */
    ESP_LOGI(TAG, "Starting BLE GATT Server...");
    if (ble_server_init() == ESP_OK) {
        ESP_LOGI(TAG, "BLE GATT Server started successfully");
    } else {
        ESP_LOGE(TAG, "Failed to start BLE GATT Server");
    }

    /* Start HTTP Server */
    http_server_start();

    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "JSON-RPC 2.0 Server Ready!");
    ESP_LOGI(TAG, "=================================================");

}
