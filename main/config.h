#ifndef CONFIG_H
#define CONFIG_H

/* GPIO Configuration */
#define GPIO_LIGHT 4

/* WiFi Configuration */
#define WIFI_MAXIMUM_RETRY 10
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASS_MAX_LEN 64

/* WiFi Provisioning Configuration */
#define PROV_QR_VERSION "v1"
#define PROV_TRANSPORT_BLE "ble"
#define PROV_TRANSPORT_SOFTAP "softap"
#define PROV_SECURITY_VERSION 2  /* 0: No security, 1: WPA2, 2: SRP6a */
#define PROV_MGR_MAX_RETRY_CNT 5

/* JSON-RPC 2.0 Error Codes */
#define JSONRPC_PARSE_ERROR -32700
#define JSONRPC_INVALID_REQUEST -32600
#define JSONRPC_METHOD_NOT_FOUND -32601
#define JSONRPC_INVALID_PARAMS -32602
#define JSONRPC_INTERNAL_ERROR -32603

/* BLE Configuration */
#define BLE_DEVICE_NAME "ShellyDevKit-ESP32C6-JSONRPC"
#define GATTS_SERVICE_UUID 0x00FF
#define GATTS_CHAR_UUID_TX_CTL 0xFF01
#define GATTS_CHAR_UUID_DATA 0xFF02
#define GATTS_CHAR_UUID_RX_CTL 0xFF03
#define GATTS_NUM_HANDLE 8
#define PROFILE_APP_ID 0
#define BLE_MTU_SIZE 512

/* BLE Manufacturer Data - ALLTERCO */
#define ALLTERCO_MFID 0x0BA9

/* BLE RPC Buffer */
#define BLE_RPC_BUFFER_SIZE 2048

#endif // CONFIG_H
