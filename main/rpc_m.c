#include "rpc_m.h"

#include "config.h"
#include "dht11.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "gpio_control.h"
#include "ota_manager.h"
#include "rpc_json.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "rpc_methods";

/* RPC Method: wifi_set - sets WiFi parameters */
static JsonRpcResponse *rpc_method_wifi_set(cJSON *params) {
    // Validate params - expecting an object with "ssid" and optional "password"
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

    ESP_LOGI(TAG, "Setting WiFi config for SSID: %s", ssid);

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

/* RPC Method: Get.SystemInfo - returns ESP32 system information */
static JsonRpcResponse *rpc_method_get_system_info(cJSON *params) {
    cJSON *info = cJSON_CreateObject();

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    cJSON_AddStringToObject(info, "model", CONFIG_IDF_TARGET);
    cJSON_AddNumberToObject(info, "cores", chip_info.cores);
    cJSON_AddNumberToObject(info, "revision", chip_info.revision);
    cJSON_AddNumberToObject(info, "free_heap", esp_get_free_heap_size());
    cJSON_AddStringToObject(info, "idf_version", esp_get_idf_version());

    return jsonrpc_response_create(info, NULL, 0);
}

/* RPC Method: light - control GPIO 4 with parameter 1/0 */
static JsonRpcResponse *rpc_method_light(cJSON *params) {
    if (params == NULL || !cJSON_IsNumber(params)) {
        return jsonrpc_response_create(NULL, "Invalid params: expected numeric 0 or 1", JSONRPC_INVALID_PARAMS);
    }

    int state = (int)params->valuedouble;
    if (state != 0 && state != 1) {
        return jsonrpc_response_create(NULL, "Invalid params: expected 0 or 1", JSONRPC_INVALID_PARAMS);
    }

    gpio_set_light_state(state);

    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "gpio", GPIO_LIGHT_4);
    cJSON_AddNumberToObject(result, "state", state);
    return jsonrpc_response_create(result, NULL, 0);
}

/* RPC Method: ble_get_info - returns BLE connection status and info */
static JsonRpcResponse *rpc_method_ble_get_info(cJSON *params) {
    cJSON *info = cJSON_CreateObject();

    // Get BLE MAC address
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    cJSON_AddStringToObject(info, "mac", mac_str);
    cJSON_AddBoolToObject(info, "enabled", true);
    cJSON_AddStringToObject(info, "status", "active");

    return jsonrpc_response_create(info, NULL, 0);
}

/* RPC Method: ota_update - triggers OTA firmware update */
static JsonRpcResponse *rpc_method_ota_update(cJSON *params) {
    if (!cJSON_IsObject(params)) {
        return jsonrpc_response_create(NULL, "Invalid params: expected an object with 'url'", JSONRPC_INVALID_PARAMS);
    }

    cJSON *url_json = cJSON_GetObjectItem(params, "url");
    if (!cJSON_IsString(url_json) || url_json->valuestring == NULL) {
        return jsonrpc_response_create(NULL, "Invalid params: 'url' is required", JSONRPC_INVALID_PARAMS);
    }

    esp_err_t err = ota_manager_start(url_json->valuestring);
    cJSON *result = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddStringToObject(result, "status", "updating");
        cJSON_AddStringToObject(result, "url", url_json->valuestring);
    } else {
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "reason", esp_err_to_name(err));
    }
    return jsonrpc_response_create(result, NULL, 0);
}

/* RPC Method: TemperatureHumidity.Get - returns temperature and humidity */
static JsonRpcResponse *rpc_method_dht11_get(cJSON *params) {
    dht11_reading_t reading;
    esp_err_t err = dht11_get_last_reading(&reading);
    if (err == ESP_ERR_NOT_FOUND) {
        cJSON *result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "status", "no_data");
        cJSON_AddStringToObject(result, "message", "DHT11 reading not ready yet");
        return jsonrpc_response_create(result, NULL, 0);
    } else if (err != ESP_OK) {
        return jsonrpc_response_create(NULL, "DHT11 read error", JSONRPC_INTERNAL_ERROR);
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "temperature", reading.temperature);
    cJSON_AddNumberToObject(result, "humidity", reading.humidity);
    cJSON_AddStringToObject(result, "unit_temp", "C");
    cJSON_AddStringToObject(result, "unit_humidity", "%");
    return jsonrpc_response_create(result, NULL, 0);
}

/* RPC Method declarations */
static JsonRpcResponse *rpc_method_shelly_list_methods(cJSON *params);

typedef struct {
    const char *method;
    rpc_handler_t handler;
} RpcMethodEntry;

static const RpcMethodEntry rpc_methods_builtin[] = {
    {"Method.List", rpc_method_shelly_list_methods},
    {"Wifi.Set", rpc_method_wifi_set},
    {"BLE.Info", rpc_method_ble_get_info},
    {"OTA.Update", rpc_method_ota_update},
    {"TemperatureHumidity.Get", rpc_method_dht11_get},
    {"System.Info", rpc_method_get_system_info},
    {"Light.Set", rpc_method_light},
};

static RpcMethodEntry *rpc_methods = NULL;
static int rpc_method_count = 0;
static int rpc_method_capacity = 0;

static void rpc_ensure_initialized(void) {
    if (rpc_methods != NULL) {
        return;
    }

    rpc_method_count = sizeof(rpc_methods_builtin) / sizeof(rpc_methods_builtin[0]);
    rpc_method_capacity = rpc_method_count + 4;
    rpc_methods = malloc(rpc_method_capacity * sizeof(RpcMethodEntry));
    if (!rpc_methods) {
        ESP_LOGE(TAG, "Failed to allocate RPC methods table");
        rpc_method_count = 0;
        rpc_method_capacity = 0;
        return;
    }

    for (int i = 0; i < rpc_method_count; i++) {
        rpc_methods[i].method = strdup(rpc_methods_builtin[i].method);
        rpc_methods[i].handler = rpc_methods_builtin[i].handler;
    }
}

void register_method(char *method_name, rpc_handler_t handler) {
    if (!method_name || !handler) {
        ESP_LOGW(TAG, "register_method: invalid args\n");
        return;
    }

    rpc_ensure_initialized();
    if (!rpc_methods) {
        return;
    }

    for (int i = 0; i < rpc_method_count; i++) {
        if (strcasecmp(method_name, rpc_methods[i].method) == 0) {
            ESP_LOGW(TAG, "register_method: method already exists: %s", method_name);
            return;
        }
    }

    if (rpc_method_count >= rpc_method_capacity) {
        int new_capacity = rpc_method_capacity + 4;
        RpcMethodEntry *new_array = realloc(rpc_methods, new_capacity * sizeof(RpcMethodEntry));
        if (!new_array) {
            ESP_LOGE(TAG, "register_method: realloc failed");
            return;
        }
        rpc_methods = new_array;
        rpc_method_capacity = new_capacity;
    }

    rpc_methods[rpc_method_count].method = strdup(method_name);
    rpc_methods[rpc_method_count].handler = handler;
    rpc_method_count++;
}

static rpc_handler_t find_rpc_handler(const char *method) {
    rpc_ensure_initialized();
    if (!rpc_methods || !method) {
        return NULL;
    }

    for (int i = 0; i < rpc_method_count; i++) {
        if (strcasecmp(method, rpc_methods[i].method) == 0) {
            return rpc_methods[i].handler;
        }
    }
    return NULL;
}

/* RPC Method: shelly_list_methods - lists available RPC methods */
static JsonRpcResponse *rpc_method_shelly_list_methods(cJSON *params) {
    rpc_ensure_initialized();
    cJSON *methods = cJSON_CreateArray();
    for (int i = 0; i < rpc_method_count; i++) {
        cJSON_AddItemToArray(methods, cJSON_CreateString(rpc_methods[i].method));
    }
    return jsonrpc_response_create(methods, NULL, 0);
}

char *rpc_process_request(const char *request_str) {
    JsonRpcRequest *request = jsonrpc_parse_request(request_str); // This will log errors if the request is invalid, but
    JsonRpcResponse *response = NULL;
    if (!request) {
        response = jsonrpc_response_create(NULL, "Parse error: invalid JSON", JSONRPC_PARSE_ERROR);
    } else if (strcmp(request->jsonrpc, "2.0") != 0) {
        response =
            jsonrpc_response_create(NULL, "Invalid Request: jsonrpc version must be '2.0'", JSONRPC_INVALID_REQUEST);
    } else if (!request->method) {
        response = jsonrpc_response_create(NULL, "Invalid Request: 'method' is required", JSONRPC_INVALID_REQUEST);
    } else {
        rpc_handler_t handler = find_rpc_handler(request->method);
        if (handler) {
            response = handler(request->params);
        } else {
            response = jsonrpc_response_create(NULL, "Method not found", JSONRPC_METHOD_NOT_FOUND);
        }
    }
    response->id = cJSON_Duplicate(request->id, true);
    char *response_str = jsonrpc_response_to_json(response);
    jsonrpc_response_free(response);
    jsonrpc_request_free(request);
    return response_str;
}
