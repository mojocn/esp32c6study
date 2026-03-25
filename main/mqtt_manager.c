#include "mqtt_manager.h"

#include "config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "rpc_m.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;
static char s_broker_host[128];

static const char *normalize_mqtt_host(const char *raw_host, uint16_t *out_port) {
  if (!raw_host || !out_port) {
    return NULL;
  }

  const char *p = raw_host;
  while (*p && isspace((unsigned char)*p)) {
    p++;
  }

  if (strncmp(p, "mqtt://", 7) == 0) {
    p += 7;
  } else if (strncmp(p, "tcp://", 6) == 0) {
    p += 6;
  }

  while (*p == '/' || *p == ':') {
    p++;
  }

  size_t i = 0;
  while (*p && *p != '/' && !isspace((unsigned char)*p) && i < sizeof(s_broker_host) - 1) {
    s_broker_host[i++] = *p++;
  }
  s_broker_host[i] = '\0';

  if (s_broker_host[0] == '\0') {
    return NULL;
  }

  char *port_sep = strrchr(s_broker_host, ':');
  if (port_sep) {
    bool numeric_port = true;
    for (char *q = port_sep + 1; *q; ++q) {
      if (!isdigit((unsigned char)*q)) {
        numeric_port = false;
        break;
      }
    }

    if (numeric_port) {
      long parsed = strtol(port_sep + 1, NULL, 10);
      if (parsed > 0 && parsed <= 65535) {
        *out_port = (uint16_t)parsed;
        *port_sep = '\0';
      }
    }
  }

  return s_broker_host;
}

/* ------------------------------------------------------------------ */
/* Topic helpers                                                        */
/* ------------------------------------------------------------------ */

static char s_topic_rpc[64];      /* <device>/rpc        – subscribe  */
static char s_topic_rpc_resp[72]; /* <device>/rpc/response – publish  */
static char s_topic_status[64];   /* <device>/status     – heartbeat  */

/* ------------------------------------------------------------------ */
/* Heartbeat timer                                                      */
/* ------------------------------------------------------------------ */

static esp_timer_handle_t s_heartbeat_timer = NULL;

static void heartbeat_cb(void *arg) {
  if (!s_connected) {
    return;
  }

  uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
  uint32_t free_heap = esp_get_free_heap_size();

  /* Build a small JSON status payload */
  char buf[160];
  snprintf(buf, sizeof(buf), "{\"uptime\":%lu,\"free_heap\":%lu,\"device\":\"%s\"}", (unsigned long)uptime_s, (unsigned long)free_heap, device_name());

  mqtt_manager_publish(s_topic_status, buf);
  ESP_LOGD(TAG, "Heartbeat: %s", buf);
}

/* ------------------------------------------------------------------ */
/* MQTT event handler                                                   */
/* ------------------------------------------------------------------ */

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

  switch ((esp_mqtt_event_id_t)event_id) {

  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "Connected to broker");
    s_connected = true;
    esp_mqtt_client_subscribe(s_client, s_topic_rpc, 1);
    ESP_LOGI(TAG, "Subscribed to %s", s_topic_rpc);

    /* Kick off heartbeat timer */
    esp_timer_start_periodic(s_heartbeat_timer, (uint64_t)MQTT_HEARTBEAT_INTERVAL_S * 1000000ULL);

    /* Publish an immediate online announcement */
    {
      char online_payload[128];
      snprintf(online_payload, sizeof(online_payload), "{\"event\":\"online\",\"device\":\"%s\"}", device_name());
      mqtt_manager_publish(s_topic_status, online_payload);
    }
    break;

  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "Disconnected from broker");
    s_connected = false;
    esp_timer_stop(s_heartbeat_timer);
    break;

  case MQTT_EVENT_DATA: {
    /* Only handle messages on the RPC topic */
    if (event->topic_len == 0) {
      break; /* fragmented or unusual delivery */
    }

    /* Null-terminate topic for comparison */
    char topic_buf[128];
    size_t tlen = (event->topic_len < sizeof(topic_buf) - 1) ? (size_t)event->topic_len : sizeof(topic_buf) - 1;
    memcpy(topic_buf, event->topic, tlen);
    topic_buf[tlen] = '\0';

    if (strcmp(topic_buf, s_topic_rpc) != 0) {
      break;
    }

    /* Null-terminate payload */
    char *payload = malloc(event->data_len + 1);
    if (!payload) {
      ESP_LOGE(TAG, "OOM handling RPC message");
      break;
    }
    memcpy(payload, event->data, event->data_len);
    payload[event->data_len] = '\0';

    ESP_LOGI(TAG, "RPC request: %s", payload);

    char *response = rpc_process_request(payload);
    free(payload);

    if (response) {
      mqtt_manager_publish(s_topic_rpc_resp, response);
      free(response);
    }
    break;
  }

  case MQTT_EVENT_ERROR:
    if (event->error_handle) {
      ESP_LOGE(TAG,
               "MQTT error: type=%d tls_last=0x%x tls_stack=0x%x "
               "transport_sock=%d cert_flags=0x%x",
               event->error_handle->error_type,
               event->error_handle->esp_tls_last_esp_err,
               event->error_handle->esp_tls_stack_err,
               event->error_handle->esp_transport_sock_errno,
               event->error_handle->esp_tls_cert_verify_flags);
    } else {
      ESP_LOGE(TAG, "MQTT error (no details)");
    }
    break;

  default:
    break;
  }
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

esp_err_t mqtt_manager_start(void) {
  /* Build topic strings */
  snprintf(s_topic_rpc, sizeof(s_topic_rpc), "%s/rpc", device_name());
  snprintf(s_topic_rpc_resp, sizeof(s_topic_rpc_resp), "%s/rpc/response", device_name());
  snprintf(s_topic_status, sizeof(s_topic_status), "%s/status", device_name());

  /* Create heartbeat timer (one-shot=false => periodic) */
  esp_timer_create_args_t timer_args = {
      .callback = heartbeat_cb,
      .arg = NULL,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "mqtt_hb",
  };
  esp_err_t err = esp_timer_create(&timer_args, &s_heartbeat_timer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create heartbeat timer: %s", esp_err_to_name(err));
    return err;
  }

  uint16_t broker_port = MQTT_BROKER_PORT;
  const char *broker_host = normalize_mqtt_host(MQTT_BROKER_HOST, &broker_port);
  if (!broker_host) {
    ESP_LOGE(TAG, "Invalid MQTT broker host: '%s'", MQTT_BROKER_HOST);
    return ESP_ERR_INVALID_ARG;
  }

  if (strcmp(broker_host, MQTT_BROKER_HOST) != 0 || broker_port != MQTT_BROKER_PORT) {
    ESP_LOGW(TAG, "Normalized MQTT broker from '%s:%d' to '%s:%u'", MQTT_BROKER_HOST, MQTT_BROKER_PORT, broker_host, broker_port);
  }

  /* Configure and start MQTT client */
  esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.hostname = broker_host,
      .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
      .broker.address.port = broker_port,
      .credentials.username = MQTT_USERNAME,
      .credentials.authentication.password = MQTT_PASSWORD,
  };

  s_client = esp_mqtt_client_init(&mqtt_cfg);
  if (!s_client) {
    ESP_LOGE(TAG, "Failed to init MQTT client");
    return ESP_FAIL;
  }

  esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

  err = esp_mqtt_client_start(s_client);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "MQTT client started, broker=%s:%u", broker_host, broker_port);
  return ESP_OK;
}

esp_err_t mqtt_manager_publish(const char *topic, const char *data) {
  if (!s_connected || !s_client) {
    return ESP_FAIL;
  }

  int msg_id = esp_mqtt_client_publish(s_client, topic, data, 0 /* use strlen */, 1 /* QoS 1 */, 0);
  return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}
