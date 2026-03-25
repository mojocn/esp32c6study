#include "rpc_m_wifi.h"

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

JsonRpcResponse *m_wifi_info(cJSON *params) {
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
        cJSON_AddNumberToObject(sta, "rssi", ap_info.rssi);
        cJSON_AddStringToObject(sta, "bssid", "TODO");
    }
    cJSON_AddItemToObject(result, "sta", sta);

    cJSON *ap = cJSON_CreateObject();
    cJSON_AddStringToObject(ap, "ssid", (char *)ap_config.ap.ssid);
    cJSON_AddNumberToObject(ap, "channel", ap_config.ap.channel);
    cJSON_AddStringToObject(ap, "authmode", authmode_to_string(ap_config.ap.authmode));
    cJSON_AddItemToObject(result, "ap", ap);

    return jsonrpc_response_create(result, NULL, 0);
}

JsonRpcResponse *m_wifi_set(cJSON *params) {
    if (!cJSON_IsObject(params)) {
        return jsonrpc_response_create(NULL, "Invalid params: expected an object with 'ssid' and optional 'password'",
                                       JSONRPC_INVALID_PARAMS);
    }

    cJSON *ssid_json = cJSON_GetObjectItem(params, "ssid");
    cJSON *password_json = cJSON_GetObjectItem(params, "password");

    if (!cJSON_IsString(ssid_json) || ssid_json->valuestring == NULL) {
        return jsonrpc_response_create(NULL, "Invalid params: 'ssid' is required and must be a string",
                                       JSONRPC_INVALID_PARAMS);
    }

    const char *ssid = ssid_json->valuestring;
    const char *password = (password_json && cJSON_IsString(password_json)) ? password_json->valuestring : "";

    // Configure WiFi settings
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = (strlen(password) > 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    // Set WiFi configuration (this also saves to NVS)
    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        return jsonrpc_response_create(NULL, "Failed to set WiFi config", JSONRPC_INTERNAL_ERROR);
    }

    // Disconnect from current network and reconnect with new credentials
    esp_wifi_disconnect();
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        return jsonrpc_response_create(NULL, "Failed to connect to WiFi with new config", JSONRPC_INTERNAL_ERROR);
    }

    // Return success response with the SSID
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "ssid", ssid);
    cJSON_AddStringToObject(result, "status", "connecting");
    return jsonrpc_response_create(result, NULL, 0);
}
