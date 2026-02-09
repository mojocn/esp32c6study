#include "jsonrpc.h"
#include "config.h"
#include "gpio_control.h"
#include "cJSON.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include <string.h>
#include <stdio.h>

/* JSON-RPC Error Response Helper */
static char *jsonrpc_error_response(int id_exists, int id_value, int code, const char *message)
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");

    cJSON *error = cJSON_CreateObject();
    cJSON_AddNumberToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    cJSON_AddItemToObject(response, "error", error);

    if (id_exists)
    {
        cJSON_AddNumberToObject(response, "id", id_value);
    }
    else
    {
        cJSON_AddNullToObject(response, "id");
    }

    char *string = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    return string;
}

/* JSON-RPC Success Response Helper */
static char *jsonrpc_success_response(int id_value, cJSON *result)
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    cJSON_AddItemToObject(response, "result", result);
    cJSON_AddNumberToObject(response, "id", id_value);

    char *string = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    return string;
}

/* RPC Method: echo - echoes back the parameter */
static cJSON *rpc_method_echo(cJSON *params)
{
    if (params == NULL)
    {
        return NULL;
    }

    // Return a copy of the params
    return cJSON_Duplicate(params, 1);
}

/* RPC Method: add - adds two numbers */
static cJSON *rpc_method_add(cJSON *params)
{
    if (!cJSON_IsArray(params) || cJSON_GetArraySize(params) != 2)
    {
        return NULL;
    }

    cJSON *a = cJSON_GetArrayItem(params, 0);
    cJSON *b = cJSON_GetArrayItem(params, 1);

    if (!cJSON_IsNumber(a) || !cJSON_IsNumber(b))
    {
        return NULL;
    }

    double result = a->valuedouble + b->valuedouble;
    return cJSON_CreateNumber(result);
}

/* RPC Method: subtract - subtracts two numbers */
static cJSON *rpc_method_subtract(cJSON *params)
{
    if (!cJSON_IsArray(params) || cJSON_GetArraySize(params) != 2)
    {
        return NULL;
    }

    cJSON *a = cJSON_GetArrayItem(params, 0);
    cJSON *b = cJSON_GetArrayItem(params, 1);

    if (!cJSON_IsNumber(a) || !cJSON_IsNumber(b))
    {
        return NULL;
    }

    double result = a->valuedouble - b->valuedouble;
    return cJSON_CreateNumber(result);
}

/* RPC Method: get_system_info - returns ESP32 system information */
static cJSON *rpc_method_get_system_info(cJSON *params)
{
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
static cJSON *rpc_method_light(cJSON *params)
{
    if (params == NULL || !cJSON_IsNumber(params))
    {
        return NULL;
    }

    int state = (int)params->valuedouble;
    if (state != 0 && state != 1)
    {
        return NULL;
    }

    gpio_set_light_state(state);

    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "gpio", GPIO_LIGHT);
    cJSON_AddNumberToObject(result, "state", state);
    return result;
}

/* RPC Method Dispatcher */
static cJSON *dispatch_method(const char *method, cJSON *params)
{
    if (strcmp(method, "echo") == 0)
    {
        return rpc_method_echo(params);
    }
    else if (strcmp(method, "add") == 0)
    {
        return rpc_method_add(params);
    }
    else if (strcmp(method, "subtract") == 0)
    {
        return rpc_method_subtract(params);
    }
    else if (strcmp(method, "get_system_info") == 0)
    {
        return rpc_method_get_system_info(params);
    }
    else if (strcmp(method, "light") == 0)
    {
        return rpc_method_light(params);
    }

    return NULL; // Method not found
}

char *jsonrpc_process_request(const char *request_str)
{
    char *response_str = NULL;

    /* Parse JSON */
    cJSON *json = cJSON_Parse(request_str);
    if (json == NULL)
    {
        return jsonrpc_error_response(0, 0, JSONRPC_PARSE_ERROR, "Parse error");
    }

    /* Validate JSON-RPC 2.0 request */
    cJSON *jsonrpc = cJSON_GetObjectItem(json, "jsonrpc");
    cJSON *method = cJSON_GetObjectItem(json, "method");
    cJSON *params = cJSON_GetObjectItem(json, "params");
    cJSON *id = cJSON_GetObjectItem(json, "id");

    int id_exists = (id != NULL && !cJSON_IsNull(id));
    int id_value = id_exists ? (int)id->valuedouble : 0;

    /* Check jsonrpc version */
    if (jsonrpc == NULL || !cJSON_IsString(jsonrpc) || strcmp(jsonrpc->valuestring, "2.0") != 0)
    {
        response_str = jsonrpc_error_response(id_exists, id_value, JSONRPC_INVALID_REQUEST, "Invalid Request");
        cJSON_Delete(json);
        return response_str;
    }

    /* Check method */
    if (method == NULL || !cJSON_IsString(method))
    {
        response_str = jsonrpc_error_response(id_exists, id_value, JSONRPC_INVALID_REQUEST, "Invalid Request");
        cJSON_Delete(json);
        return response_str;
    }

    /* If it's a notification (no id), we still process but don't respond */
    bool is_notification = !id_exists;

    /* Dispatch method */
    cJSON *result = dispatch_method(method->valuestring, params);

    if (result == NULL)
    {
        /* Method not found or invalid parameters */
        if (!is_notification)
        {
            if (strcmp(method->valuestring, "echo") != 0 &&
                strcmp(method->valuestring, "add") != 0 &&
                strcmp(method->valuestring, "subtract") != 0 &&
                strcmp(method->valuestring, "get_system_info") != 0 &&
                strcmp(method->valuestring, "light") != 0)
            {
                response_str = jsonrpc_error_response(id_exists, id_value, JSONRPC_METHOD_NOT_FOUND, "Method not found");
            }
            else
            {
                response_str = jsonrpc_error_response(id_exists, id_value, JSONRPC_INVALID_PARAMS, "Invalid params");
            }
        }
    }
    else
    {
        /* Success */
        if (!is_notification)
        {
            response_str = jsonrpc_success_response(id_value, result);
        }
        else
        {
            cJSON_Delete(result);
        }
    }

    cJSON_Delete(json);
    return response_str;
}
