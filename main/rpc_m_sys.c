#include "rpc_m_sys.h"

#include "esp_chip_info.h"
#include "esp_system.h"
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
