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
#include "rpc_m_ble.h"
#include "rpc_m_ht.h"
#include "rpc_m_light.h"
#include "rpc_m_ota.h"
#include "rpc_m_sys.h"
#include "rpc_m_wifi.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* RPC Method declarations */
static JsonRpcResponse *m_sys_methods(cJSON *params);

typedef struct {
    const char *method;
    rpc_handler_t handler;
} RpcMethodEntry;

static const RpcMethodEntry rpc_methods[] = {
    {"Sys.Info", m_sys_info}, {"Sys.Methods", m_sys_methods}, {"Wifi.Set", m_wifi_set}, {"Wifi.Info", m_wifi_info},
    {"BLE.Info", m_ble_info}, {"OTA.Update", m_ota_update},   {"Ht.Info", m_ht_info},   {"Light.Set", m_light_set},
};
static int rpc_method_count = sizeof(rpc_methods) / sizeof(rpc_methods[0]);

static rpc_handler_t find_rpc_handler(const char *method) {
    if (!method) {
        return NULL;
    }
    for (int i = 0; i < rpc_method_count; i++) {
        if (strcasecmp(method, rpc_methods[i].method) == 0) {
            return rpc_methods[i].handler;
        }
    }
    return NULL;
}

static JsonRpcResponse *m_sys_methods(cJSON *params) {
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
