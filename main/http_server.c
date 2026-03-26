#include "http_server.h"

#include "config.h"
#include "esp_log.h"
#include "rpc_m.h"

#include <stdlib.h>

static const char *TAG = "HTTP";

/* JSON-RPC Request Handler */
#define JSONRPC_MAX_BODY 2048

static esp_err_t jsonrpc_handler(httpd_req_t *req) {
  char *buf = NULL;
  char *response_str = NULL;
  size_t buf_len = req->content_len;

  if (buf_len == 0) {
    httpd_resp_set_type(req, "application/json");

    httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32600,\"message\":\"Empty request\"},\"id\":null}");
    return ESP_OK;
  }

  if (buf_len > JSONRPC_MAX_BODY) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "413 Payload Too Large");
    httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32600,\"message\":\"Request body too large\"},\"id\":null}");
    return ESP_OK;
  }

  buf = malloc(buf_len + 1);
  if (buf == NULL) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32603,\"message\":\"Memory allocation failed\"},\"id\":null}");
    return ESP_OK;
  }

  size_t received = 0;
  while (received < buf_len) {
    int ret = httpd_req_recv(req, buf + received, buf_len - received);
    if (ret <= 0) {
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
        continue; /* Retry until content_len bytes are read */
      }
      free(buf);
      return ESP_FAIL;
    }
    received += (size_t)ret;
  }
  buf[received] = '\0';

  ESP_LOGI(TAG, "HTTP Received: %s", buf);

  response_str = rpc_process_request(buf);
  free(buf);

  if (response_str != NULL) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response_str);
    free(response_str);
  } else {
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
  }

  return ESP_OK;
}

httpd_handle_t http_server_start(void) {
  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.max_uri_handlers = 8;
  config.max_resp_headers = 8;
  config.lru_purge_enable = true;

  ESP_LOGI(TAG, "Starting HTTP Server on port: '%d'", config.server_port);
  if (httpd_start(&server, &config) == ESP_OK) {
    /* Register URI handlers */
    httpd_uri_t jsonrpc_uri = {.uri = "/rpc", .method = HTTP_POST, .handler = jsonrpc_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &jsonrpc_uri);

    ESP_LOGI(TAG, "HTTP Server started - JSON-RPC endpoint: http://<ip>/rpc");
    return server;
  }

  ESP_LOGI(TAG, "Error starting HTTP Server!");
  return NULL;
}
