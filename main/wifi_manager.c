

#include "config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "WiFi";

/* Event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_DISCONNECTED_BIT = BIT1;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    static int retry_num = 0;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "WiFi AP started");
                break;
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
                ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d", MAC2STR(event->mac), event->aid);
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
                ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d", MAC2STR(event->mac), event->aid);
                break;
            }
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA started, attempting to connect...");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                if (retry_num < WIFI_MAXIMUM_RETRY) {
                    esp_wifi_connect();
                    retry_num++;
                    ESP_LOGI(TAG, "Retry to connect to the AP (attempt %d/%d)", retry_num, WIFI_MAXIMUM_RETRY);
                } else {
                    ESP_LOGI(TAG, "Failed to connect to AP after %d attempts", WIFI_MAXIMUM_RETRY);
                    xEventGroupSetBits(wifi_event_group, WIFI_DISCONNECTED_BIT);
                }
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to AP");
                retry_num = 0;
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void initialise_wifi(void) {
    /* Initialize TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Create event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Create WiFi event group */
    wifi_event_group = xEventGroupCreate();

    /* Create AP and STA interfaces */
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    /* Initialize WiFi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register WiFi event handlers */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    /* Get device MAC address for AP name */
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));

    /* Configure WiFi AP */
    wifi_config_t ap_config = {
        .ap =
            {
                .ssid_len = 0,
                .channel = 1,
                .authmode = WIFI_AUTH_OPEN,
                .max_connection = 4,
            },
    };
    snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), "%s_%02X%02X%02X", DEVICE_NAME, mac[3], mac[4],
             mac[5]);

    /* Configure WiFi STA (will try to connect if credentials exist in NVS) */
    wifi_config_t sta_config = {0};
    esp_wifi_get_config(WIFI_IF_STA, &sta_config);

    /* Set WiFi mode to AP+STA */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    /* Start WiFi */
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "=== WiFi Initialized ===");
    ESP_LOGI(TAG, "AP SSID: %s", ap_config.ap.ssid);
    ESP_LOGI(TAG, "AP Password: (none)");
    ESP_LOGI(TAG, "AP IP: 192.168.4.1");
    if (strlen((char *)sta_config.sta.ssid) > 0) {
        ESP_LOGI(TAG, "STA will connect to: %s", sta_config.sta.ssid);
    } else {
        ESP_LOGI(TAG, "STA: No saved credentials, use Wifi.set RPC method");
    }
    ESP_LOGI(TAG, "========================");
}
