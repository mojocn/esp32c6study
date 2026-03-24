#include "jsonrpc.h"

#include "cJSON.h"
#include "config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "gpio_control.h"
#include "jsonrpc_methods.h"

#include <stdio.h>
#include <string.h>
#define TAG "JSONRPC"
/* JSON-RPC Error Response Helper */
static char *jsonrpc_error_response(int id_exists, int id_value, int code, const char *message) {
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");

    cJSON *error = cJSON_CreateObject();
    cJSON_AddNumberToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    cJSON_AddItemToObject(response, "error", error);

    if (id_exists) {
        cJSON_AddNumberToObject(response, "id", id_value);
    } else {
        cJSON_AddNullToObject(response, "id");
    }

    char *string = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    return string;
}

/* JSON-RPC Success Response Helper */
static char *jsonrpc_success_response(int id_value, cJSON *result) {
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    cJSON_AddItemToObject(response, "result", result);
    cJSON_AddNumberToObject(response, "id", id_value);

    char *string = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    return string;
}

char *jsonrpc_process_request(const char *request_str) {
    char *response_str = NULL;

    /* Parse JSON */
    cJSON *json = cJSON_Parse(request_str);
    if (json == NULL) {
        return jsonrpc_error_response(0, 0, JSONRPC_PARSE_ERROR, "Parse error");
    }

    /* Validate JSON-RPC 2.0 request */
    cJSON *jsonrpc = cJSON_GetObjectItem(json, "jsonrpc");
    cJSON *method = cJSON_GetObjectItem(json, "method");
    cJSON *params = cJSON_GetObjectItem(json, "params");
    cJSON *id = cJSON_GetObjectItem(json, "id");

    int id_exists = (id != NULL && !cJSON_IsNull(id));
    int id_value = id_exists ? (int)id->valuedouble : 0;

    if (cJSON_IsString(jsonrpc) && strcmp(jsonrpc->valuestring, "2.0") != 0) {
        response_str = jsonrpc_error_response(id_exists, id_value, JSONRPC_INVALID_REQUEST,
                                              "Invalid Request: jsonrpc version must be '2.0'");
        cJSON_Delete(json);
        return response_str;
    }

    /* Check method */
    if (method == NULL || !cJSON_IsString(method)) {
        response_str = jsonrpc_error_response(id_exists, id_value, JSONRPC_INVALID_REQUEST,
                                              "Invalid Request: 'method' must be a string");
        cJSON_Delete(json);
        return response_str;
    }

    /* Dispatch method */
    cJSON *result = dispatch_method(method->valuestring, params);

    if (result == NULL) {
        /* Method not found or invalid parameters */
        response_str = jsonrpc_error_response(id_exists, id_value, JSONRPC_INTERNAL_ERROR,
                                              "Internal result is null (method not found or invalid params)");
    } else {
        response_str = jsonrpc_success_response(id_value, result);
    }
    cJSON_Delete(json);
    return response_str;
}
