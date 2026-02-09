#ifndef JSONRPC_H
#define JSONRPC_H

/**
 * @brief Process a JSON-RPC request and return response string
 * @param request_str JSON-RPC request string
 * @return Response string (must be freed by caller), or NULL for notifications
 */
char *jsonrpc_process_request(const char *request_str);

#endif // JSONRPC_H
