/* JSON-RPC 2.0 HTTP/BLE Server for ESP32-C6 */

#include <stdio.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"
#include "store.h"
#include "gpio_control.h"
#include "wifi_manager.h"
#include "ble_server.h"
#include "http_server.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "JSON-RPC 2.0 HTTP/BLE Server starting...");

    /* Initialize NVS */
    init_nvs();

    /* Initialize GPIO */
    gpio_control_init();

    /* Initialize RGB LED */
    rgb_led_init();

    /* Initialize WiFi (must be before BLE to allow BLE provisioning if needed) */
    wifi_manager_init();

    /* Initialize BLE (after WiFi is connected) */
    ble_server_init();

    /* Get and print IP address */
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK)
    {
        ESP_LOGI(TAG, "Device IP Address: " IPSTR, IP2STR(&ip_info.ip));
    }

    /* Start HTTP Server */
    http_server_start();

    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "JSON-RPC 2.0 Server Ready!");
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "HTTP Endpoint:");
    ESP_LOGI(TAG, "  Connected to WiFi: %s", wifi_manager_get_ssid());
    ESP_LOGI(TAG, "  Device IP: " IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "  Endpoint: http://" IPSTR "/rpc", IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "BLE Endpoint:");
    ESP_LOGI(TAG, "  Device Name: %s", BLE_DEVICE_NAME);
    ESP_LOGI(TAG, "  Service UUID: 5f6d4f53-5f52-5043-5f53-56435f49445f");
    ESP_LOGI(TAG, "  TX_CTL UUID:  5f6d4f53-5f52-5043-5f74-785f63746c5f (Write)");
    ESP_LOGI(TAG, "  DATA UUID:    5f6d4f53-5f52-5043-5f64-6174615f5f5f (Read/Write)");
    ESP_LOGI(TAG, "  RX_CTL UUID:  5f6d4f53-5f52-5043-5f72-785f63746c5f (Read/Notify)");
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "Available methods:");
    ESP_LOGI(TAG, "  - echo: Echo back parameters");
    ESP_LOGI(TAG, "  - add: Add two numbers [a, b]");
    ESP_LOGI(TAG, "  - subtract: Subtract two numbers [a, b]");
    ESP_LOGI(TAG, "  - get_system_info: Get ESP32 system info");
    ESP_LOGI(TAG, "  - light: Control GPIO 4 (params: 0 or 1)");
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "Example request:");
    ESP_LOGI(TAG, "{\"jsonrpc\":\"2.0\",\"method\":\"add\",\"params\":[5,3],\"id\":1}");
    ESP_LOGI(TAG, "=================================================");
}
