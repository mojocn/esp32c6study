#ifndef CONFIG_H
#define CONFIG_H

/* WiFi Configuration */
#define WIFI_MAXIMUM_RETRY 10
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASS_MAX_LEN 64
#define WIFI_DEFAULT_SSID "Shelly Asia"
#define WIFI_DEFAULT_PASS "Asia20211220"

/* WiFi Provisioning Configuration */
#define PROV_QR_VERSION "v1"
#define PROV_TRANSPORT_BLE "ble"
#define PROV_TRANSPORT_SOFTAP "softap"
#define PROV_SECURITY_VERSION 2 /* 0: No security, 1: WPA2, 2: SRP6a */
#define PROV_MGR_MAX_RETRY_CNT 5

/* JSON-RPC 2.0 Error Codes */
#define JSONRPC_PARSE_ERROR -32700
#define JSONRPC_INVALID_REQUEST -32600
#define JSONRPC_METHOD_NOT_FOUND -32601
#define JSONRPC_INVALID_PARAMS -32602
#define JSONRPC_INTERNAL_ERROR -32603
/* WiFi AP SSID Configuration */
#define DEVICE_NAME "ShellyEric"
#define WIFI_AP_PASS ""

/* MQTT Configuration */
#define MQTT_BROKER_HOST "mqtt.shellyiot.cn"
#define MQTT_BROKER_PORT 1883
#define MQTT_USERNAME "admin"
#define MQTT_PASSWORD "ZHou20170928"
#define MQTT_HEARTBEAT_INTERVAL_S 10 /* Heartbeat every 10 seconds */

#endif // CONFIG_H
