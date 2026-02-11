

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
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

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

/* WiFi Provisioning Event Handler */
static void prov_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "WiFi provisioning started");
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received WiFi credentials - SSID: %s", (const char *)wifi_sta_cfg->ssid);
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Provisioning failed: %s",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "WiFi station authentication failed"
                                                               : "WiFi AP not found");
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                break;
            case WIFI_PROV_END:
                ESP_LOGI(TAG, "Provisioning ended");
                wifi_prov_mgr_deinit();
                break;
            default:
                break;
        }
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

    /* Check if WiFi credentials are provisioned */
    bool provisioned = false;
    wifi_config_t sta_check_config = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &sta_check_config) == ESP_OK) {
        if (strlen((char *)sta_check_config.sta.ssid) > 0) {
            provisioned = true;
        }
    }

    if (!provisioned) {
        ESP_LOGI(TAG, "No WiFi credentials found, starting BLE provisioning");

        /* Configure provisioning manager */
        wifi_prov_mgr_config_t prov_config = {
            .scheme = wifi_prov_scheme_ble,
            .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
        };

        ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_config));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler, NULL));

        /* Generate device name */
        char device_name[32];
        snprintf(device_name, sizeof(device_name), "PROV_%s_%02X%02X%02X", DEVICE_NAME, mac[3], mac[4], mac[5]);

        /* Start provisioning with proof-of-possession */
        const char *pop = "abcd1234";
        ESP_LOGI(TAG, "Starting BLE provisioning with PoP: %s", pop);
        ESP_LOGI(TAG, "Device name: %s", device_name);

        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, pop, device_name, NULL));

        ESP_LOGI(TAG, "=== WiFi Provisioning Mode ===");
        ESP_LOGI(TAG, "Use ESP BLE Provisioning app to configure WiFi");
        ESP_LOGI(TAG, "Device: %s", device_name);
        ESP_LOGI(TAG, "PoP: %s", pop);
        ESP_LOGI(TAG, "==============================");
    } else {
        ESP_LOGI(TAG, "WiFi credentials found in NVS");

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
        ESP_LOGI(TAG, "STA will connect to: %s", sta_config.sta.ssid);
        ESP_LOGI(TAG, "========================");
    }
}
