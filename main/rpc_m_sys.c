#include "rpc_m_sys.h"

#include "cJSON.h"
#include "esp_chip_info.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "ota_manager.h"
#include "sdkconfig.h"

JsonRpcResponse *m_sys_info(cJSON *params) {
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

JsonRpcResponse *m_sys_ota(cJSON *params) {
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
