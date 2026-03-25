#include "rpc_m_wifi.h"
#include "config.h"
#include "wifi_manager.h"

#include "rpc_m.h"

#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>

#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

static void format_mac_address(const uint8_t *mac, char *out, size_t out_size) {
  if (!mac || !out || out_size < 18) {
    if (out && out_size > 0) {
      out[0] = '\0';
    }
    return;
  }
  snprintf(out, out_size, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

JsonRpcResponse *m_wifi_sta_info(cJSON *params) {
  wifi_mode_t mode;
  esp_err_t err = esp_wifi_get_mode(&mode);
  if (err != ESP_OK) {
    return jsonrpc_response_create(NULL, "Failed to get WiFi mode", JSONRPC_INTERNAL_ERROR);
  }

  wifi_config_t sta_config = {0};
  err = esp_wifi_get_config(WIFI_IF_STA, &sta_config);
  if (err != ESP_OK) {
    return jsonrpc_response_create(NULL, "Failed to get STA WiFi config", JSONRPC_INTERNAL_ERROR);
  }

  wifi_config_t ap_config = {0};
  err = esp_wifi_get_config(WIFI_IF_AP, &ap_config);
  if (err != ESP_OK) {
    return jsonrpc_response_create(NULL, "Failed to get AP WiFi config", JSONRPC_INTERNAL_ERROR);
  }

  wifi_ap_record_t ap_info;
  bool sta_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);

  cJSON *result = cJSON_CreateObject();
  cJSON_AddStringToObject(result, "mode", wifi_mode_to_string(mode));

  cJSON *sta = cJSON_CreateObject();
  cJSON_AddStringToObject(sta, "ssid", (char *)sta_config.sta.ssid);
  cJSON_AddStringToObject(sta, "authmode", authmode_to_string(sta_config.sta.threshold.authmode));
  cJSON_AddStringToObject(sta, "status", sta_connected ? "connected" : "disconnected");
  if (sta_connected) {
    char bssid[18];
    format_mac_address(ap_info.bssid, bssid, sizeof(bssid));
    cJSON_AddNumberToObject(sta, "rssi", ap_info.rssi);
    cJSON_AddStringToObject(sta, "bssid", bssid);
  }
  cJSON_AddItemToObject(result, "sta", sta);

  cJSON *ap = cJSON_CreateObject();
  cJSON_AddStringToObject(ap, "ssid", (char *)ap_config.ap.ssid);
  cJSON_AddNumberToObject(ap, "channel", ap_config.ap.channel);
  cJSON_AddStringToObject(ap, "authmode", authmode_to_string(ap_config.ap.authmode));
  cJSON_AddItemToObject(result, "ap", ap);

  return jsonrpc_response_create(result, NULL, 0);
}

JsonRpcResponse *m_wifi_ap_info(cJSON *params) {
  (void)params;

  wifi_mode_t mode;
  esp_err_t err = esp_wifi_get_mode(&mode);
  if (err != ESP_OK) {
    return jsonrpc_response_create(NULL, "Failed to get WiFi mode", JSONRPC_INTERNAL_ERROR);
  }

  wifi_config_t ap_config = {0};
  err = esp_wifi_get_config(WIFI_IF_AP, &ap_config);
  if (err != ESP_OK) {
    return jsonrpc_response_create(NULL, "Failed to get AP WiFi config", JSONRPC_INTERNAL_ERROR);
  }

  cJSON *result = cJSON_CreateObject();
  cJSON_AddStringToObject(result, "mode", wifi_mode_to_string(mode));

  cJSON *ap = cJSON_CreateObject();
  cJSON_AddStringToObject(ap, "ssid", (char *)ap_config.ap.ssid);
  cJSON_AddNumberToObject(ap, "channel", ap_config.ap.channel);
  cJSON_AddStringToObject(ap, "authmode", authmode_to_string(ap_config.ap.authmode));
  cJSON_AddNumberToObject(ap, "max_connection", ap_config.ap.max_connection);
  cJSON_AddItemToObject(result, "ap", ap);

  wifi_sta_list_t sta_list = {0};
  err = esp_wifi_ap_get_sta_list(&sta_list);
  cJSON *clients = cJSON_CreateArray();
  if (err == ESP_OK) {
    for (int i = 0; i < sta_list.num; i++) {
      wifi_sta_info_t *sta_info = &sta_list.sta[i];
      char bssid[18] = {0};
      snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X", sta_info->mac[0], sta_info->mac[1], sta_info->mac[2], sta_info->mac[3], sta_info->mac[4], sta_info->mac[5]);

      cJSON *client = cJSON_CreateObject();
      cJSON_AddStringToObject(client, "bssid", bssid);
      cJSON_AddNumberToObject(client, "rssi", sta_info->rssi);
      cJSON_AddItemToArray(clients, client);
    }
  } else {
    cJSON_AddStringToObject(result, "clients_error", "Failed to get AP station list");
  }

  cJSON_AddItemToObject(result, "clients", clients);
  return jsonrpc_response_create(result, NULL, 0);
}

JsonRpcResponse *m_wifi_sta_set(cJSON *params) {
  if (!cJSON_IsObject(params)) {
    return jsonrpc_response_create(NULL, "Invalid params: expected an object with 'enable' and optional 'ssid','password'", JSONRPC_INVALID_PARAMS);
  }

  cJSON *ssid_json = cJSON_GetObjectItem(params, "ssid");
  cJSON *password_json = cJSON_GetObjectItem(params, "password");
  cJSON *enable_json = cJSON_GetObjectItem(params, "enable");

  if (!cJSON_IsBool(enable_json)) {
    return jsonrpc_response_create(NULL, "Invalid params: 'enable' is required and must be a boolean", JSONRPC_INVALID_PARAMS);
  }
  if (!cJSON_IsString(ssid_json) || !ssid_json->valuestring) {
    return jsonrpc_response_create(NULL, "Invalid params: 'ssid' is required and must be a string", JSONRPC_INVALID_PARAMS);
  }

  const bool enable = cJSON_IsTrue(enable_json);
  const char *ssid = ssid_json->valuestring;
  const char *password = (password_json && cJSON_IsString(password_json)) ? password_json->valuestring : "";

  AppConfig *config = config_get();
  if (!config) {
    return jsonrpc_response_create(NULL, "Failed to load config", JSONRPC_INTERNAL_ERROR);
  }
  config->wifi_sta_enabled = enable;
  strncpy(config->wifi_sta_ssid, ssid, sizeof(config->wifi_sta_ssid) - 1);
  strncpy(config->wifi_sta_password, password, sizeof(config->wifi_sta_password) - 1);
  config_save(config);
  wifi_config_apply(config);
  config_free(config);

  cJSON *result = cJSON_CreateObject();
  cJSON_AddStringToObject(result, "ssid", ssid);
  cJSON_AddStringToObject(result, "status", "connecting");
  return jsonrpc_response_create(result, NULL, 0);
}

JsonRpcResponse *m_wifi_ap_set(cJSON *params) {
  if (!cJSON_IsObject(params)) {
    return jsonrpc_response_create(NULL, "Invalid params: expected an object with 'enable' and optional 'password','ssid'", JSONRPC_INVALID_PARAMS);
  }
  cJSON *enable_json = cJSON_GetObjectItem(params, "enable");
  cJSON *ssid_json = cJSON_GetObjectItem(params, "ssid");
  cJSON *password_json = cJSON_GetObjectItem(params, "password");

  if (!cJSON_IsBool(enable_json)) {
    return jsonrpc_response_create(NULL, "Invalid params: 'enable' is required and must be a boolean", JSONRPC_INVALID_PARAMS);
  }

  bool enable = cJSON_IsTrue(enable_json);
  const char *ssid = (ssid_json && cJSON_IsString(ssid_json)) ? ssid_json->valuestring : device_name();
  const char *password = (password_json && cJSON_IsString(password_json)) ? password_json->valuestring : "";

  AppConfig *config = config_get();
  if (!config) {
    return jsonrpc_response_create(NULL, "Failed to load config", JSONRPC_INTERNAL_ERROR);
  }
  config->wifi_ap_enabled = enable;
  strncpy(config->wifi_ap_ssid, ssid, sizeof(config->wifi_ap_ssid) - 1);
  strncpy(config->wifi_ap_password, password, sizeof(config->wifi_ap_password) - 1);
  config_save(config);
  wifi_config_apply(config);
  config_free(config);

  cJSON *result = cJSON_CreateObject();
  cJSON_AddStringToObject(result, "ssid", ssid);
  cJSON_AddStringToObject(result, "status", "AP enabled");
  return jsonrpc_response_create(result, NULL, 0);
}
