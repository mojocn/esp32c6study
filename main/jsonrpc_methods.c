#include "jsonrpc_methods.h"

#include "config.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "gpio_control.h"

#include <string.h>

static const char *TAG = "jsonrpc_methods";

/* RPC Method: shelly_list_methods - lists available RPC methods */
static cJSON *rpc_method_shelly_list_methods(cJSON *params) {
    cJSON *result = cJSON_CreateObject();
    cJSON *methods = cJSON_CreateArray();

    cJSON_AddItemToArray(methods, cJSON_CreateString("EM1Data.GetNetEnergies"));
    cJSON_AddItemToArray(methods, cJSON_CreateString("EM1Data.SetConfig"));

    cJSON_AddItemToObject(result, "methods", methods);

    return result;
}

/* RPC Method: wifi_set - sets WiFi parameters */
static cJSON *rpc_method_wifi_set(cJSON *params) {
    // Validate params - expecting an object with "ssid" and optional "password"
    if (!cJSON_IsObject(params)) {
        ESP_LOGE(TAG, "wifi_set: params must be an object");
        return NULL;
    }

    cJSON *ssid_json = cJSON_GetObjectItem(params, "ssid");
    cJSON *password_json = cJSON_GetObjectItem(params, "password");

    if (!cJSON_IsString(ssid_json) || ssid_json->valuestring == NULL) {
        ESP_LOGE(TAG, "wifi_set: ssid is required and must be a string");
        return NULL;
    }

    const char *ssid = ssid_json->valuestring;
    const char *password = (password_json && cJSON_IsString(password_json)) ? password_json->valuestring : "";

    // Configure WiFi settings
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = (strlen(password) > 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_LOGI(TAG, "Setting WiFi config for SSID: %s", ssid);

    // Set WiFi configuration (this also saves to NVS)
    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(ret));
        return NULL;
    }

    // Disconnect from current network and reconnect with new credentials
    esp_wifi_disconnect();
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initiate WiFi connection: %s", esp_err_to_name(ret));
        return NULL;
    }

    // Return success response with the SSID
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "ssid", ssid);
    cJSON_AddStringToObject(result, "status", "connecting");
    return result;
}

/* RPC Method: subtract - subtracts two numbers */
static cJSON *rpc_method_subtract(cJSON *params) {
    if (!cJSON_IsArray(params) || cJSON_GetArraySize(params) != 2) {
        return NULL;
    }

    cJSON *a = cJSON_GetArrayItem(params, 0);
    cJSON *b = cJSON_GetArrayItem(params, 1);

    if (!cJSON_IsNumber(a) || !cJSON_IsNumber(b)) {
        return NULL;
    }

    double result = a->valuedouble - b->valuedouble;
    return cJSON_CreateNumber(result);
}

/* RPC Method: get_system_info - returns ESP32 system information */
static cJSON *rpc_method_get_system_info(cJSON *params) {
    cJSON *info = cJSON_CreateObject();

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    cJSON_AddStringToObject(info, "model", CONFIG_IDF_TARGET);
    cJSON_AddNumberToObject(info, "cores", chip_info.cores);
    cJSON_AddNumberToObject(info, "revision", chip_info.revision);
    cJSON_AddNumberToObject(info, "free_heap", esp_get_free_heap_size());
    cJSON_AddStringToObject(info, "idf_version", esp_get_idf_version());

    return info;
}

/* RPC Method: light - control GPIO 4 with parameter 1/0 */
static cJSON *rpc_method_light(cJSON *params) {
    if (params == NULL || !cJSON_IsNumber(params)) {
        return NULL;
    }

    int state = (int)params->valuedouble;
    if (state != 0 && state != 1) {
        return NULL;
    }

    gpio_set_light_state(state);

    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "gpio", GPIO_LIGHT_4);
    cJSON_AddNumberToObject(result, "state", state);
    return result;
}

/* RPC Method Dispatcher */

cJSON *dispatch_method(const char *method, cJSON *params) {
    if (strcmp(method, "Shelly.ListMethods") == 0) {
        return rpc_method_shelly_list_methods(params);
    } else if (strcmp(method, "Wifi.set") == 0) {
        return rpc_method_wifi_set(params);
    } else if (strcmp(method, "subtract") == 0) {
        return rpc_method_subtract(params);
    } else if (strcmp(method, "get_system_info") == 0) {
        return rpc_method_get_system_info(params);
    } else if (strcmp(method, "light") == 0) {
        return rpc_method_light(params);
    }

    return NULL; // Method not found
}
