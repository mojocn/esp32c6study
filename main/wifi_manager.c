#include "wifi_manager.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define WIFI_CONN_MAX_RETRY 5

static const char *TAG = "WiFiManager";
static int s_retry_num = 0;

static char sta_ip[16] = ""; // 仅用于存储 STA IP 字符串，避免写入只读内存

// WiFi 事件处理函数（核心：处理STA/AP所有状态）
static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  // ---------------- STA 事件 ----------------
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect(); // STA启动后自动连接路由器
    ESP_LOGI(TAG, "STA 模式启动，正在连接路由器...");
    sta_ip[0] = '\0'; // 清空IP地址
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
    ESP_LOGI(TAG, "成功连接到路由器 (STA_CONNECTED)");
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < WIFI_CONN_MAX_RETRY) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "路由器断开，重连中... 第 %d 次", s_retry_num);
    } else {
      ESP_LOGI(TAG, "重连失败，停止自动重连");
    }
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_AUTHMODE_CHANGE) {
    wifi_event_sta_authmode_change_t *event = (wifi_event_sta_authmode_change_t *)event_data;
    (void)event;
    ESP_LOGI(TAG, "路由器认证模式改变: %d -> %d", event->old_mode, event->new_mode);
  }

  // ---------------- AP 事件 ----------------
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
    // ESP_LOGI(TAG, "AP 模式启动，热点名称: %s", WIFI_AP_SSID);

  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
    wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
    (void)event;
    // ESP_LOGI(TAG, "设备已连接 AP, MAC: " MACSTR ", 在线数: %d", MAC2STR(event->mac), esp_wifi_ap_get_sta_count());
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
    (void)event;
    // ESP_LOGI(TAG, "设备断开 AP, MAC: " MACSTR ", 在线数: %d", MAC2STR(event->mac), esp_wifi_ap_get_sta_count());
  }

  // ---------------- IP 事件 ----------------
  else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "STA 获取IP: " IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0; // 重连计数清零
  }
}

void wifi_config_apply(const AppConfig *config) {
  if (!config) {
    ESP_LOGE(TAG, "wifi_config_apply: config pointer is NULL");
    return;
  }

  wifi_config_t cfg = {0};
  cfg.ap.max_connection = 4;
  cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
  cfg.ap.authmode = WIFI_AUTH_OPEN;

  ESP_LOGI(TAG, "STA SSID: %s", config->wifi_sta_ssid ? config->wifi_sta_ssid : "<none>");
  ESP_LOGI(TAG, "STA Password: %s", config->wifi_sta_password ? config->wifi_sta_password : "<none>");

  if (config->wifi_sta_ssid) {
    char sta_ssid_buf[sizeof(cfg.sta.ssid)];
    strncpy(sta_ssid_buf, config->wifi_sta_ssid, sizeof(sta_ssid_buf) - 1);
    sta_ssid_buf[sizeof(sta_ssid_buf) - 1] = '\0';
    /* Trim trailing whitespace/newline characters often present when reading from files */
    size_t ssid_len = strlen(sta_ssid_buf);
    while (ssid_len > 0 && isspace((unsigned char)sta_ssid_buf[ssid_len - 1])) {
      sta_ssid_buf[--ssid_len] = '\0';
    }
    strncpy((char *)cfg.sta.ssid, sta_ssid_buf, sizeof(cfg.sta.ssid) - 1);
    cfg.sta.ssid[sizeof(cfg.sta.ssid) - 1] = '\0';
  }

  if (config->wifi_sta_password && config->wifi_sta_password[0] != '\0') {
    char sta_pwd_buf[sizeof(cfg.sta.password)];
    strncpy(sta_pwd_buf, config->wifi_sta_password, sizeof(sta_pwd_buf) - 1);
    sta_pwd_buf[sizeof(sta_pwd_buf) - 1] = '\0';
    /* Trim trailing whitespace/newline characters often present when reading from files */
    size_t len = strlen(sta_pwd_buf);
    while (len > 0 && isspace((unsigned char)sta_pwd_buf[len - 1])) {
      sta_pwd_buf[--len] = '\0';
    }
    size_t sta_pwd_len = len;
    if (sta_pwd_len >= 8) {
      strncpy((char *)cfg.sta.password, sta_pwd_buf, sizeof(cfg.sta.password) - 1);
      cfg.sta.password[sizeof(cfg.sta.password) - 1] = '\0';
      cfg.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    } else {
      ESP_LOGW(TAG, "STA password too short (%zu). Falling back to OPEN auth.", sta_pwd_len);
      cfg.sta.password[0] = '\0';
      cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
  }

  // Debug output for STA credentials
  ESP_LOGI(TAG, "Configured STA SSID: %s", cfg.sta.ssid[0] ? (char *)cfg.sta.ssid : "<none>");
  ESP_LOGI(TAG, "Configured STA Password: %s", cfg.sta.password[0] ? (char *)cfg.sta.password : "<none>");

  if (config->wifi_ap_ssid) {
    strncpy((char *)cfg.ap.ssid, config->wifi_ap_ssid, sizeof(cfg.ap.ssid) - 1);
    cfg.ap.ssid[sizeof(cfg.ap.ssid) - 1] = '\0';
  }

  if (config->wifi_ap_password && config->wifi_ap_password[0] != '\0') {
    size_t ap_pwd_len = strnlen(config->wifi_ap_password, sizeof(cfg.ap.password));
    if (ap_pwd_len >= 8) {
      strncpy((char *)cfg.ap.password, config->wifi_ap_password, sizeof(cfg.ap.password) - 1);
      cfg.ap.password[sizeof(cfg.ap.password) - 1] = '\0';
      cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
      ESP_LOGW(TAG, "AP password too short (%u). Using OPEN AP (no password).", ap_pwd_len);
      cfg.ap.password[0] = '\0';
      cfg.ap.authmode = WIFI_AUTH_OPEN;
    }
  }

  wifi_mode_t mode = WIFI_MODE_NULL;
  if (config->wifi_ap_enabled && config->wifi_sta_enabled) {
    mode = WIFI_MODE_APSTA;
  } else if (config->wifi_ap_enabled) {
    mode = WIFI_MODE_AP;
  } else if (config->wifi_sta_enabled) {
    mode = WIFI_MODE_STA;
  } else {
    mode = WIFI_MODE_NULL;
  }

  esp_err_t err = esp_wifi_set_mode(mode);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
    return;
  }

  if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
    err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_wifi_set_config(WIFI_IF_STA) failed: %s", esp_err_to_name(err));
      return;
    }
  }
  if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
    err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_wifi_set_config(WIFI_IF_AP) failed: %s", esp_err_to_name(err));
      return;
    }
  }

  if (mode != WIFI_MODE_NULL) {
    err = esp_wifi_start();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
      return;
    }
  } else {
    ESP_LOGW(TAG, "WiFi mode is NULL, not starting WiFi");
  }
}

void wifi_init() {
  // 1. 创建 WiFi 驱动任务
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // 2. 创建默认网络接口（必须：STA+AP各创建一个）
  esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();

  // 3. WiFi 默认初始化配置
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // 4. 注册所有 WiFi/IP 事件
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));
}
