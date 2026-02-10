
#ifndef JSONRPC_METHODS_H
#define JSONRPC_METHODS_H

#include "cJSON.h"

cJSON *dispatch_method(const char *method, cJSON *params);

#endif // JSONRPC_METHODS_H