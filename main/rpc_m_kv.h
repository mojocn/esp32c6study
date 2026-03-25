#ifndef RPC_M_KV_H
#define RPC_M_KV_H

#include "cJSON.h"
#include "rpc_json.h"

JsonRpcResponse *m_kv_keys(cJSON *params);
JsonRpcResponse *m_kv_set(cJSON *params);
JsonRpcResponse *m_kv_get(cJSON *params);

#endif // RPC_M_KV_H
