#include "rpc_m_light.h"

#include "config.h"
#include "gpio_control.h"

JsonRpcResponse *m_light_set(cJSON *params) {
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
