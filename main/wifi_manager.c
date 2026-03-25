

#include "cJSON.h"
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

void initialise_wifi(AppConfig *config) {
  if (!config) {
    ESP_LOGW(TAG, "No configuration provided, using defaults");
    return;
  }

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

  snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), "%s", device_name());
  ESP_LOGI(TAG, "Configuring WiFi AP with SSID: " MACSTR, MAC2STR(mac));
  ESP_LOGI(TAG, "Configuring WiFi AP with SSID: %s", ap_config.ap.ssid);

  /* Configure WiFi STA: use NVS credentials if saved, otherwise fall back to defaults */
  wifi_config_t sta_config = {0};
  esp_wifi_get_config(WIFI_IF_STA, &sta_config);
  if (strlen((char *)sta_config.sta.ssid) == 0) {
    strncpy((char *)sta_config.sta.ssid, config->wifi_sta_ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, config->wifi_sta_password, sizeof(sta_config.sta.password) - 1);
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_LOGI(TAG, "No NVS credentials found, using default WiFi: %s", config->wifi_sta_ssid);
  } else {
    ESP_LOGI(TAG, "WiFi credentials found in NVS: %s", sta_config.sta.ssid);
  }

  bool sta_enabled = config->wifi_sta_enabled;

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

  if (sta_enabled) {
    ESP_LOGI(TAG, "STA functionality is enabled (device boot). Will use SSID: %s", sta_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
  } else {
    ESP_LOGI(TAG, "STA functionality is disabled (device boot). Starting AP-only mode.");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  }

  /* Start WiFi */
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "=== WiFi Initialized ===");
  ESP_LOGI(TAG, "AP SSID: %s", ap_config.ap.ssid);
  ESP_LOGI(TAG, "AP Password: (none)");
  ESP_LOGI(TAG, "AP IP: 192.168.4.1");
  if (sta_enabled) {
    ESP_LOGI(TAG, "STA will connect to: %s", sta_config.sta.ssid);
  } else {
    ESP_LOGI(TAG, "STA disabled on boot");
  }
  ESP_LOGI(TAG, "========================");
}

cJSON *wifi_sta_init(bool enabled, char *ssid, char *password) {
  cJSON *result = cJSON_CreateObject();
  if (!result) {
    return NULL;
  }

  if (!enabled) {

    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
      cJSON_AddStringToObject(result, "error", "Failed to disconnect STA");
      cJSON_AddStringToObject(result, "status", "disabled");
      return result;
    }

    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
      if (mode == WIFI_MODE_APSTA) {
        esp_wifi_set_mode(WIFI_MODE_AP);
      } else if (mode == WIFI_MODE_STA) {
        esp_wifi_set_mode(WIFI_MODE_NULL);
      }
    }

    cJSON_AddStringToObject(result, "status", "disabled");
    return result;
  }

  if (!ssid || ssid[0] == '\0') {
    cJSON_AddStringToObject(result, "error", "SSID is required when enabling STA");
    return result;
  }

  wifi_config_t wifi_config = {0};
  strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
  if (password && password[0] != '\0') {
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  } else {
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
  }

  esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  if (err != ESP_OK) {
    cJSON_AddStringToObject(result, "error", "Failed to set STA config");
    return result;
  }

  wifi_mode_t mode;
  if (esp_wifi_get_mode(&mode) == ESP_OK) {
    if (mode == WIFI_MODE_NULL) {
      esp_wifi_set_mode(WIFI_MODE_STA);
    } else if (mode == WIFI_MODE_AP) {
      esp_wifi_set_mode(WIFI_MODE_APSTA);
    }
  }

  esp_wifi_disconnect();
  err = esp_wifi_connect();
  if (err != ESP_OK) {
    cJSON_AddStringToObject(result, "error", "Failed to connect STA");
    return result;
  }

  cJSON_AddStringToObject(result, "status", "connecting");
  cJSON_AddStringToObject(result, "ssid", ssid);
  return result;
}

cJSON *wifi_ap_init(bool enabled, char *ssid, char *password) {
  cJSON *result = cJSON_CreateObject();
  if (!result) {
    return NULL;
  }

  if (!enabled) {
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
      if (mode == WIFI_MODE_APSTA) {
        esp_wifi_set_mode(WIFI_MODE_STA);
      } else if (mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_NULL);
      }
    }

    cJSON_AddStringToObject(result, "status", "disabled");
    return result;
  }

  if (!ssid || ssid[0] == '\0') {
    cJSON_AddStringToObject(result, "error", "SSID is required when enabling AP");
    return result;
  }

  wifi_config_t wifi_config = {0};
  strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
  wifi_config.ap.ssid_len = strnlen((char *)wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid));
  wifi_config.ap.channel = 1;
  wifi_config.ap.max_connection = 4;

  if (password && password[0] != '\0') {
    strncpy((char *)wifi_config.ap.password, password, sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  } else {
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  }

  esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
  if (err != ESP_OK) {
    cJSON_AddStringToObject(result, "error", "Failed to set AP config");
    return result;
  }

  wifi_mode_t mode;
  if (esp_wifi_get_mode(&mode) == ESP_OK) {
    if (mode == WIFI_MODE_NULL) {
      esp_wifi_set_mode(WIFI_MODE_AP);
    } else if (mode == WIFI_MODE_STA) {
      esp_wifi_set_mode(WIFI_MODE_APSTA);
    }
  }

  err = esp_wifi_start();
  if (err != ESP_OK) {
    cJSON_AddStringToObject(result, "error", "Failed to start AP");
    return result;
  }

  cJSON_AddStringToObject(result, "status", "AP enabled");
  cJSON_AddStringToObject(result, "ssid", ssid);
  return result;
}
