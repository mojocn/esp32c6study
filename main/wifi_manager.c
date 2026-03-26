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
static bool s_wifi_started = false;
static bool s_wifi_reconfiguring = false;

static char sta_ip[16] = ""; // 仅用于存储 STA IP 字符串，避免写入只读内存

static size_t copy_trimmed_value(char *dest, size_t dest_size, const char *src, const char *label) {
  if (!dest || dest_size == 0) {
    return 0;
  }

  dest[0] = '\0';
  if (!src) {
    return 0;
  }

  size_t src_len = strlen(src);
  if (src_len >= dest_size) {
    ESP_LOGW(TAG, "%s is too long (%zu), truncating to %zu bytes", label, src_len, dest_size - 1);
    src_len = dest_size - 1;
  }

  memcpy(dest, src, src_len);
  dest[src_len] = '\0';

  while (src_len > 0 && isspace((unsigned char)dest[src_len - 1])) {
    dest[--src_len] = '\0';
  }

  return src_len;
}

// WiFi 事件处理函数（核心：处理STA/AP所有状态）
static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  (void)arg;

  // ---------------- STA 事件 ----------------
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_err_t err = esp_wifi_connect(); // STA启动后自动连接路由器
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "STA 模式启动，正在连接路由器...");
    } else {
      ESP_LOGE(TAG, "STA 启动后连接失败: %s", esp_err_to_name(err));
    }
    sta_ip[0] = '\0'; // 清空IP地址
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
    ESP_LOGI(TAG, "成功连接到路由器 (STA_CONNECTED)");
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
    sta_ip[0] = '\0';

    if (s_wifi_reconfiguring) {
      ESP_LOGI(TAG, "STA 断开连接（正在重配 WiFi），reason=%d", event ? event->reason : -1);
      return;
    }

    if (s_retry_num < WIFI_CONN_MAX_RETRY) {
      esp_err_t err = esp_wifi_connect();
      if (err == ESP_OK) {
        s_retry_num++;
        ESP_LOGW(TAG, "路由器断开，重连中... 第 %d 次，reason=%d", s_retry_num, event ? event->reason : -1);
      } else {
        ESP_LOGE(TAG, "路由器断开后重连失败: %s, reason=%d", esp_err_to_name(err), event ? event->reason : -1);
      }
    } else {
      ESP_LOGE(TAG, "重连失败，停止自动重连，reason=%d", event ? event->reason : -1);
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
    snprintf(sta_ip, sizeof(sta_ip), IPSTR, IP2STR(&event->ip_info.ip));
    ESP_LOGI(TAG, "STA 获取IP: %s", sta_ip);
    s_retry_num = 0; // 重连计数清零
  }
}

void wifi_config_apply(const AppConfig *config) {
  if (!config) {
    ESP_LOGE(TAG, "wifi_config_apply: config pointer is NULL");
    return;
  }

  wifi_config_t sta_cfg = {0};
  wifi_config_t ap_cfg = {0};
  ap_cfg.ap.max_connection = 4;
  ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
  sta_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  sta_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  sta_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
  sta_cfg.sta.pmf_cfg.capable = true;
  sta_cfg.sta.pmf_cfg.required = false;

  ESP_LOGI(TAG, "STA SSID: %s", config->wifi_sta_ssid ? config->wifi_sta_ssid : "<none>");
  ESP_LOGI(TAG, "STA Password: %s", (config->wifi_sta_password && config->wifi_sta_password[0] != '\0') ? "<set>" : "<none>");

  if (config->wifi_sta_ssid) {
    copy_trimmed_value((char *)sta_cfg.sta.ssid, sizeof(sta_cfg.sta.ssid), config->wifi_sta_ssid, "STA SSID");
  }

  if (config->wifi_sta_password && config->wifi_sta_password[0] != '\0') {
    size_t sta_pwd_len =
        copy_trimmed_value((char *)sta_cfg.sta.password, sizeof(sta_cfg.sta.password), config->wifi_sta_password, "STA password");
    if (sta_pwd_len >= 8) {
      /* WPA2 threshold keeps WPA2/WPA3 routers connectable without rejecting WPA2-only APs. */
      sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
      ESP_LOGW(TAG, "STA password too short (%zu). Falling back to OPEN auth.", sta_pwd_len);
      sta_cfg.sta.password[0] = '\0';
      sta_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
  }

  // Debug output for STA credentials
  ESP_LOGI(TAG, "Configured STA SSID: %s", sta_cfg.sta.ssid[0] ? (char *)sta_cfg.sta.ssid : "<none>");
  ESP_LOGI(TAG, "Configured STA Password: %s", sta_cfg.sta.password[0] ? "<set>" : "<none>");

  if (config->wifi_ap_ssid) {
    copy_trimmed_value((char *)ap_cfg.ap.ssid, sizeof(ap_cfg.ap.ssid), config->wifi_ap_ssid, "AP SSID");
  }

  if (config->wifi_ap_password && config->wifi_ap_password[0] != '\0') {
    size_t ap_pwd_len =
        copy_trimmed_value((char *)ap_cfg.ap.password, sizeof(ap_cfg.ap.password), config->wifi_ap_password, "AP password");
    if (ap_pwd_len >= 8) {
      ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
      ESP_LOGW(TAG, "AP password too short (%zu). Using OPEN AP (no password).", ap_pwd_len);
      ap_cfg.ap.password[0] = '\0';
      ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
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

  esp_err_t err = ESP_OK;
  s_wifi_reconfiguring = true;
  if (s_wifi_started) {
    err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
      ESP_LOGE(TAG, "esp_wifi_stop failed: %s", esp_err_to_name(err));
      s_wifi_reconfiguring = false;
      return;
    }
    s_wifi_started = false;
  }

  err = esp_wifi_set_mode(mode);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
    s_wifi_reconfiguring = false;
    return;
  }

  if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
    err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_wifi_set_config(WIFI_IF_STA) failed: %s", esp_err_to_name(err));
      s_wifi_reconfiguring = false;
      return;
    }
  }
  if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_wifi_set_config(WIFI_IF_AP) failed: %s", esp_err_to_name(err));
      s_wifi_reconfiguring = false;
      return;
    }
  }

  if (mode != WIFI_MODE_NULL) {
    s_retry_num = 0;
    err = esp_wifi_start();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
      s_wifi_reconfiguring = false;
      return;
    }
    s_wifi_started = true;
  } else {
    s_retry_num = 0;
    sta_ip[0] = '\0';
    ESP_LOGW(TAG, "WiFi mode is NULL, not starting WiFi");
  }

  s_wifi_reconfiguring = false;
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
