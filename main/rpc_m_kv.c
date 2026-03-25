#include "rpc_m_kv.h"

#include "cJSON.h"
#include "rpc_json.h"
#include "store.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

JsonRpcResponse *m_kv_keys(cJSON *params) {
    (void)params;

    char **keys = store_keys();
    cJSON *result = cJSON_CreateArray();

    if (keys) {
        for (size_t i = 0; keys[i] != NULL; i++) {
            cJSON_AddItemToArray(result, cJSON_CreateString(keys[i]));
            free(keys[i]);
        }
        free(keys);
    }

    return jsonrpc_response_create(result, NULL, 0);
}

static bool parse_key_param(cJSON *params, char **out_key, char **out_err) {
    if (!params || !cJSON_IsObject(params)) {
        *out_err = "Invalid params: expected object with 'key'";
        return false;
    }

    cJSON *key_item = cJSON_GetObjectItem(params, "key");
    if (!key_item || !cJSON_IsString(key_item) || key_item->valuestring == NULL) {
        *out_err = "Invalid params: 'key' must be a string";
        return false;
    }

    *out_key = key_item->valuestring;
    return true;
}

JsonRpcResponse *m_kv_set(cJSON *params) {
    char *key = NULL;
    char *err_msg = NULL;

    if (!parse_key_param(params, &key, &err_msg)) {
        return jsonrpc_response_create(NULL, err_msg, JSONRPC_INVALID_PARAMS);
    }

    cJSON *value_item = cJSON_GetObjectItem(params, "value");
    if (!value_item) {
        return jsonrpc_response_create(NULL, "Invalid params: 'value' is required", JSONRPC_INVALID_PARAMS);
    }

    if (cJSON_IsString(value_item) && value_item->valuestring != NULL) {
        store_str_set(key, value_item->valuestring);
    } else {
        char *encoded = cJSON_PrintUnformatted(value_item);
        if (!encoded) {
            return jsonrpc_response_create(NULL, "Failed to encode value", JSONRPC_INTERNAL_ERROR);
        }
        store_str_set(key, encoded);
        free(encoded);
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "key", key);
    cJSON_AddBoolToObject(result, "ok", true);
    return jsonrpc_response_create(result, NULL, 0);
}

JsonRpcResponse *m_kv_get(cJSON *params) {
    char *key = NULL;
    char *err_msg = NULL;

    if (!parse_key_param(params, &key, &err_msg)) {
        return jsonrpc_response_create(NULL, err_msg, JSONRPC_INVALID_PARAMS);
    }

    char *stored = store_str_get(key);
    if (!stored) {
        return jsonrpc_response_create(NULL, "Key not found", JSONRPC_INVALID_PARAMS);
    }

    cJSON *json_value = cJSON_Parse(stored);
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "key", key);

    if (json_value) {
        cJSON_AddItemToObject(result, "value", json_value);
    } else {
        cJSON_AddStringToObject(result, "value", stored);
    }

    free(stored);
    return jsonrpc_response_create(result, NULL, 0);
}
