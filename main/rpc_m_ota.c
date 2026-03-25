#include "rpc_m_ota.h"

#include "cJSON.h"
#include "ota_manager.h"
#include "rpc_json.h"

/* RPC Method: OTA.Update - triggers OTA firmware update */
JsonRpcResponse *m_ota_update(cJSON *params) {
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
