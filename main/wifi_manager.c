#include "wifi_manager.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include <string.h>

static const char *TAG = "WiFi";
static char wifi_ssid[WIFI_SSID_MAX_LEN] = {0};

/* FreeRTOS event group to signal when we are connected */
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_PROV_BIT BIT2

static int s_retry_num = 0;
static bool s_provisioned = false;

/* WiFi Event Handler */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < WIFI_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP (attempt %d/%d)", s_retry_num, WIFI_MAXIMUM_RETRY);
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGI(TAG, "Failed to connect to AP");
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* Provisioning Event Handler */
static void prov_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_PROV_EVENT)
    {
        switch (event_id)
        {
        case WIFI_PROV_START:
            ESP_LOGI(TAG, "Provisioning started");
            break;
        case WIFI_PROV_CRED_RECV:
        {
            wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
            ESP_LOGI(TAG, "Received Wi-Fi credentials - SSID: %s", (const char *)wifi_sta_cfg->ssid);
            memcpy(wifi_ssid, wifi_sta_cfg->ssid, sizeof(wifi_ssid));
            break;
        }
        case WIFI_PROV_CRED_FAIL:
        {
            wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
            ESP_LOGE(TAG, "Provisioning failed! Reason: %s",
                     (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Wi-Fi AP password incorrect" : "Wi-Fi AP not found");
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            break;
        }
        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning successful");
            s_provisioned = true;
            xEventGroupSetBits(s_wifi_event_group, WIFI_PROV_BIT);
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

/* Get device service name for provisioning */
static void get_device_service_name(char *service_name, size_t max)
{
    uint8_t eth_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max, "%s_%02X%02X%02X",
             BLE_DEVICE_NAME, eth_mac[3], eth_mac[4], eth_mac[5]);
}

esp_err_t wifi_manager_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    /* Initialize TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Initialize the event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Register event handlers */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler, NULL));

    /* Initialize Wi-Fi including netif with default config */
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Configuration for the provisioning manager */
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM
    };

    /* Initialize provisioning manager */
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    /* Check if device is provisioned */
    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    if (!provisioned)
    {
        ESP_LOGI(TAG, "Starting provisioning via BLE");

        /* Generate device name based on MAC */
        char service_name[32];
        get_device_service_name(service_name, sizeof(service_name));

        /* Security version 2 with default proof of possession */
        const char *pop = "abcd1234";  // Proof of Possession - change this!
        
        /* Optional: Set a custom service UUID for BLE */
        uint8_t custom_service_uuid[] = {
            0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
            0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
        };
        
        wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);

        /* Start provisioning service */
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(PROV_SECURITY_VERSION, pop, service_name, NULL));

        /* Print QR code for BLE provisioning */
        ESP_LOGI(TAG, "=== Provisioning Info ===");
        ESP_LOGI(TAG, "Device Name: %s", service_name);
        ESP_LOGI(TAG, "Proof of Possession (PoP): %s", pop);
        ESP_LOGI(TAG, "Scan QR code or use ESP BLE Provisioning app:");
        ESP_LOGI(TAG, "========================");

        /* Wait for provisioning to complete */
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                               WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_PROV_BIT,
                                               pdFALSE,
                                               pdFALSE,
                                               portMAX_DELAY);

        if (bits & WIFI_CONNECTED_BIT)
        {
            ESP_LOGI(TAG, "Connected to AP via provisioning");
            s_provisioned = true;
            return ESP_OK;
        }
        else
        {
            ESP_LOGE(TAG, "Failed to provision device");
            return ESP_FAIL;
        }
    }
    else
    {
        ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi STA");

        /* Get Wi-Fi SSID from NVS */
        wifi_config_t wifi_cfg;
        if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) == ESP_OK)
        {
            memcpy(wifi_ssid, wifi_cfg.sta.ssid, sizeof(wifi_ssid));
            ESP_LOGI(TAG, "Found credentials for SSID: %s", wifi_ssid);
        }

        /* Start Wi-Fi in station mode */
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());

        /* We don't need the provisioning manager anymore */
        wifi_prov_mgr_deinit();

        /* Wait for connection */
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                               WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                               pdFALSE,
                                               pdFALSE,
                                               portMAX_DELAY);

        if (bits & WIFI_CONNECTED_BIT)
        {
            ESP_LOGI(TAG, "Connected to AP: %s", wifi_ssid);
            s_provisioned = true;
            return ESP_OK;
        }
        else
        {
            ESP_LOGE(TAG, "Failed to connect to AP: %s", wifi_ssid);
            return ESP_FAIL;
        }
    }
}

bool wifi_manager_is_provisioned(void)
{
    return s_provisioned;
}

esp_err_t wifi_manager_reset_provisioning(void)
{
    ESP_LOGI(TAG, "Resetting provisioning data...");
    esp_err_t ret = wifi_prov_mgr_reset_provisioning();
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Provisioning reset successful. Please restart the device.");
        s_provisioned = false;
        memset(wifi_ssid, 0, sizeof(wifi_ssid));
    }
    return ret;
}

void *wifi_manager_get_event_group(void)
{
    return s_wifi_event_group;
}

const char *wifi_manager_get_ssid(void)
{
    return wifi_ssid;
}
