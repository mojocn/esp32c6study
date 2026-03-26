#include "wifi_manager.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define WIFI_CONN_MAX_RETRY 5

static const char *TAG = "WiFiManager";
static int s_retry_num = 0;
static char sta_ip[16] = ""; // 仅用于存储 STA IP 字符串，避免写入只读内存

static void format_mac_address_local(const uint8_t *mac, char *out, size_t out_size) {
  if (!mac || !out || out_size < 18) {
    if (out && out_size > 0) {
      out[0] = '\0';
    }
    return;
  }
  snprintf(out, out_size, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static size_t str_trim_copy(char *dest, size_t dest_size, const char *src) {
  if (!dest || dest_size == 0) {
    return 0;
  }

  dest[0] = '\0';
  if (!src) {
    return 0;
  }

  size_t src_len = strlen(src);
  if (src_len >= dest_size) {
    ESP_LOGW(TAG, "Value is too long (%zu), truncating to %zu bytes", src_len, dest_size - 1);
    src_len = dest_size - 1;
  }

  memcpy(dest, src, src_len);
  dest[src_len] = '\0';

  while (src_len > 0 && isspace((unsigned char)dest[src_len - 1])) {
    dest[--src_len] = '\0';
  }

  return src_len;
}

static const char *wifi_mode_to_string(wifi_mode_t mode) {
  switch (mode) {
  case WIFI_MODE_NULL:
    return "NULL";
  case WIFI_MODE_STA:
    return "STA";
  case WIFI_MODE_AP:
    return "AP";
  case WIFI_MODE_APSTA:
    return "APSTA";
  default:
    return "UNKNOWN";
  }
}

static const char *authmode_to_string(wifi_auth_mode_t authmode) {
  switch (authmode) {
  case WIFI_AUTH_OPEN:
    return "OPEN";
  case WIFI_AUTH_WEP:
    return "WEP";
  case WIFI_AUTH_WPA_PSK:
    return "WPA_PSK";
  case WIFI_AUTH_WPA2_PSK:
    return "WPA2_PSK";
  case WIFI_AUTH_WPA_WPA2_PSK:
    return "WPA_WPA2_PSK";
  case WIFI_AUTH_WPA2_ENTERPRISE:
    return "WPA2_ENTERPRISE";
  case WIFI_AUTH_WPA3_PSK:
    return "WPA3_PSK";
  case WIFI_AUTH_WPA2_WPA3_PSK:
    return "WPA2_WPA3_PSK";
  default:
    return "UNKNOWN";
  }
}

static esp_err_t wifi_stop_if_started(void) {
  esp_err_t err = esp_wifi_stop();
  if (err == ESP_OK || err == ESP_ERR_WIFI_NOT_STARTED) {
    return ESP_OK;
  }

  ESP_LOGE(TAG, "esp_wifi_stop failed: %s", esp_err_to_name(err));
  return err;
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
    if (event) {
      ESP_LOGI(TAG, "路由器认证模式改变: %d -> %d", event->old_mode, event->new_mode);
    } else {
      ESP_LOGI(TAG, "路由器认证模式改变");
    }
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
    if (event) {
      snprintf(sta_ip, sizeof(sta_ip), IPSTR, IP2STR(&event->ip_info.ip));
      ESP_LOGI(TAG, "STA 获取IP: %s", sta_ip);
      s_retry_num = 0; // 重连计数清零
    }
  }
}

void wifi_config_apply(const AppConfig *config) {
  if (!config) {
    ESP_LOGE(TAG, "wifi_config_apply: config pointer is NULL");
    return;
  }

  const char *sta_ssid = config->wifi_sta_ssid;
  const char *ap_ssid = config->wifi_ap_ssid;
  if (config->wifi_ap_enabled && (!ap_ssid || ap_ssid[0] == '\0')) {
    ap_ssid = device_name();
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

  if (sta_ssid && sta_ssid[0] != '\0') {
    str_trim_copy((char *)sta_cfg.sta.ssid, sizeof(sta_cfg.sta.ssid), sta_ssid);
  }

  if (config->wifi_sta_password && config->wifi_sta_password[0] != '\0') {
    size_t sta_pwd_len = str_trim_copy((char *)sta_cfg.sta.password, sizeof(sta_cfg.sta.password), config->wifi_sta_password);
    if (sta_pwd_len >= 8) {
      /* WPA2 threshold keeps WPA2/WPA3 routers connectable without rejecting WPA2-only APs. */
      sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
      ESP_LOGW(TAG, "STA password too short (%zu). Falling back to OPEN auth.", sta_pwd_len);
      sta_cfg.sta.password[0] = '\0';
      sta_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
  }

  if (ap_ssid && ap_ssid[0] != '\0') {
    str_trim_copy((char *)ap_cfg.ap.ssid, sizeof(ap_cfg.ap.ssid), ap_ssid);
    ap_cfg.ap.ssid_len = strlen((char *)ap_cfg.ap.ssid);
  } else if (config->wifi_ap_enabled) {
    ESP_LOGW(TAG, "AP is enabled but SSID is empty");
  }

  if (config->wifi_ap_password && config->wifi_ap_password[0] != '\0') {
    size_t ap_pwd_len = str_trim_copy((char *)ap_cfg.ap.password, sizeof(ap_cfg.ap.password), config->wifi_ap_password);
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
  }

  if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
    if (sta_cfg.sta.ssid[0] == '\0') {
      ESP_LOGE(TAG, "STA is enabled but SSID is empty");
      return;
    }
  }

  if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
    if (ap_cfg.ap.ssid[0] == '\0') {
      ESP_LOGE(TAG, "AP is enabled but SSID is empty");
      return;
    }
  }

  s_retry_num = 0;
  esp_err_t err = wifi_stop_if_started();
  if (err != ESP_OK) {
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(20));

  err = esp_wifi_set_mode(mode);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_set_mode(%s) failed: %s", wifi_mode_to_string(mode), esp_err_to_name(err));
    return;
  }

  if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
    err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_wifi_set_config(WIFI_IF_STA) failed: %s", esp_err_to_name(err));
      return;
    }
  }

  if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
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
    sta_ip[0] = '\0';
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

cJSON *wifi_status() {
  cJSON *root = cJSON_CreateObject();
  if (!root) {
    return NULL;
  }

  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) == ESP_OK) {
    cJSON_AddStringToObject(root, "mode", wifi_mode_to_string(mode));
  }

  // STA info
  wifi_config_t cfg = {0};
  if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK) {
    cJSON *sta = cJSON_CreateObject();
    if (sta) {
      if (cfg.sta.ssid[0]) {
        cJSON_AddStringToObject(sta, "ssid", (const char *)cfg.sta.ssid);
      }
      if (cfg.sta.password[0]) {
        cJSON_AddStringToObject(sta, "password", "***");
      }
      cJSON_AddStringToObject(sta, "authmode", authmode_to_string(cfg.sta.threshold.authmode));
      cJSON_AddStringToObject(sta, "status", sta_ip[0] ? "connected" : "disconnected");

      uint8_t mac[6] = {0};
      char macstr[18] = "";
      if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        format_mac_address_local(mac, macstr, sizeof(macstr));
        cJSON_AddStringToObject(sta, "mac", macstr);
      }

      esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
      if (sta_netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK) {
          char ipbuf[16] = "";
          snprintf(ipbuf, sizeof(ipbuf), IPSTR, IP2STR(&ip_info.ip));
          cJSON_AddStringToObject(sta, "ip", ipbuf);

          char gwbuf[16] = "";
          snprintf(gwbuf, sizeof(gwbuf), IPSTR, IP2STR(&ip_info.gw));
          cJSON_AddStringToObject(sta, "gateway", gwbuf);
        }

        esp_netif_dns_info_t dns;
        if (esp_netif_get_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK) {
          char dnsbuf[16] = "";
          snprintf(dnsbuf, sizeof(dnsbuf), IPSTR, IP2STR(&dns.ip.u_addr.ip4));
          cJSON_AddStringToObject(sta, "dns_main", dnsbuf);
        }
        if (esp_netif_get_dns_info(sta_netif, ESP_NETIF_DNS_BACKUP, &dns) == ESP_OK) {
          char dnsbuf2[16] = "";
          snprintf(dnsbuf2, sizeof(dnsbuf2), IPSTR, IP2STR(&dns.ip.u_addr.ip4));
          cJSON_AddStringToObject(sta, "dns_backup", dnsbuf2);
        }
      }

      cJSON_AddItemToObject(root, "sta", sta);
    }
  }

  // AP info
  if (esp_wifi_get_config(WIFI_IF_AP, &cfg) == ESP_OK) {
    cJSON *ap = cJSON_CreateObject();
    if (ap) {
      if (cfg.ap.ssid[0]) {
        cJSON_AddStringToObject(ap, "ssid", (const char *)cfg.ap.ssid);
      }
      if (cfg.ap.password[0]) {
        cJSON_AddStringToObject(ap, "password", "***");
      }
      cJSON_AddStringToObject(ap, "authmode", authmode_to_string(cfg.ap.authmode));
      cJSON_AddNumberToObject(ap, "channel", cfg.ap.channel);
      cJSON_AddNumberToObject(ap, "max_connection", cfg.ap.max_connection);

      uint8_t mac[6] = {0};
      char macstr[18] = "";
      if (esp_wifi_get_mac(WIFI_IF_AP, mac) == ESP_OK) {
        format_mac_address_local(mac, macstr, sizeof(macstr));
        cJSON_AddStringToObject(ap, "mac", macstr);
      }

      esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
      if (ap_netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
          char ipbuf[16] = "";
          snprintf(ipbuf, sizeof(ipbuf), IPSTR, IP2STR(&ip_info.ip));
          cJSON_AddStringToObject(ap, "ip", ipbuf);

          char gwbuf[16] = "";
          snprintf(gwbuf, sizeof(gwbuf), IPSTR, IP2STR(&ip_info.gw));
          cJSON_AddStringToObject(ap, "gateway", gwbuf);
        }
      }

      cJSON *clients = cJSON_CreateArray();
      if (clients) {
        wifi_sta_list_t sta_list = {0};
        if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
          for (int i = 0; i < sta_list.num; i++) {
            cJSON *client = cJSON_CreateObject();
            if (!client) {
              continue;
            }

            char bssid[18] = {0};
            format_mac_address_local(sta_list.sta[i].mac, bssid, sizeof(bssid));
            cJSON_AddStringToObject(client, "bssid", bssid);
            cJSON_AddNumberToObject(client, "rssi", sta_list.sta[i].rssi);
            cJSON_AddItemToArray(clients, client);
          }
        }
        cJSON_AddItemToObject(ap, "clients", clients);
      }

      cJSON_AddItemToObject(root, "ap", ap);
    }
  }

  return root;
}
