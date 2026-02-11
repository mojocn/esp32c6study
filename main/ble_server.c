#include "ble_server.h"

#include "config.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "jsonrpc.h"

#include <string.h>

static const char *TAG = "BLE_SERVER";

/* BLE Service and Characteristic UUIDs (Shelly Gen3 Official UUIDs) */
/* GATT_SERVICE_UUID: "5f6d4f53-5f52-5043-5f53-56435f49445f" */
static const uint8_t SERVICE_UUID[16] = {0x5f, 0x44, 0x49, 0x5f, 0x43, 0x56, 0x53, 0x5f,
                                         0x43, 0x50, 0x52, 0x5f, 0x53, 0x4f, 0x6d, 0x5f};

/* RPC_CHAR_TX_CTL_UUID: "5f6d4f53-5f52-5043-5f74-785f63746c5f" (Write) */
static const uint8_t CHAR_TX_CTL_UUID[16] = {0x5f, 0x6c, 0x74, 0x63, 0x5f, 0x78, 0x74, 0x5f,
                                             0x43, 0x50, 0x52, 0x5f, 0x53, 0x4f, 0x6d, 0x5f};

/* RPC_CHAR_RX_CTL_UUID: "5f6d4f53-5f52-5043-5f72-785f63746c5f" (Read/Notify) */
static const uint8_t CHAR_RX_CTL_UUID[16] = {0x5f, 0x6c, 0x74, 0x63, 0x5f, 0x78, 0x72, 0x5f,
                                             0x43, 0x50, 0x52, 0x5f, 0x53, 0x4f, 0x6d, 0x5f};

/* RPC_CHAR_DATA_UUID: "5f6d4f53-5f52-5043-5f64-6174615f5f5f" (Read/Write) */
static const uint8_t CHAR_DATA_UUID[16] = {0x5f, 0x5f, 0x5f, 0x61, 0x74, 0x61, 0x64, 0x5f,
                                           0x43, 0x50, 0x52, 0x5f, 0x53, 0x4f, 0x6d, 0x5f};

/* Allterco Robotics manufacturer ID */
#define ALLTERCO_MFID 0x0BA9

/* GATT Interface and Connection State */
static uint16_t gatt_if_global = ESP_GATT_IF_NONE;
static uint16_t conn_id_global = 0xFFFF;
static uint16_t service_handle = 0;
static uint16_t char_tx_handle = 0;
static uint16_t char_rx_handle = 0;
static uint16_t char_data_handle = 0;
static bool is_connected = false;
static uint16_t mtu_size = 23; // Default BLE MTU

/* Attribute handles */
enum {
    IDX_SVC,
    IDX_CHAR_TX_CTL,
    IDX_CHAR_TX_CTL_VAL,
    IDX_CHAR_RX_CTL,
    IDX_CHAR_RX_CTL_VAL,
    IDX_CHAR_RX_CTL_CFG,
    IDX_CHAR_DATA,
    IDX_CHAR_DATA_VAL,
    IDX_NB,
};

static uint16_t attr_handle_table[IDX_NB];

/* Manufacturer data: Allterco Robotics */
static uint8_t manufacturer_data[2] = {
    ALLTERCO_MFID & 0xFF,       // Low byte
    (ALLTERCO_MFID >> 8) & 0xFF // High byte
};

/* BLE Advertising */
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x20,
    .max_interval = 0x40,
    .appearance = 0x00,
    .manufacturer_len = sizeof(manufacturer_data),
    .p_manufacturer_data = manufacturer_data,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 16,
    .p_service_uuid = (uint8_t *)SERVICE_UUID,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

/* Data reception and response state */
#define MAX_DATA_SIZE 4096
static uint8_t rx_buffer[MAX_DATA_SIZE];
static uint32_t rx_expected_len = 0; // Length from TX_CTL
static uint32_t rx_received_len = 0; // Bytes received in DATA

static uint8_t tx_buffer[MAX_DATA_SIZE]; // Response buffer
static uint32_t tx_data_len = 0;         // Response data length
static uint32_t tx_data_offset = 0;      // Current read offset

/**
 * @brief Process complete JSON-RPC request and prepare response
 */
static void process_request(void) {
    // Null-terminate the received data
    rx_buffer[rx_received_len] = '\0';

    ESP_LOGI(TAG, "Received JSON-RPC request (%lu bytes): %s", rx_received_len, (char *)rx_buffer);

    // Process JSON-RPC request
    char *json_response = jsonrpc_process_request((char *)rx_buffer);
    if (json_response == NULL) {
        ESP_LOGE(TAG, "Failed to process JSON-RPC request");
        return;
    }

    // Store response in tx_buffer
    tx_data_len = strlen(json_response);
    if (tx_data_len > MAX_DATA_SIZE) {
        ESP_LOGE(TAG, "Response too large: %lu bytes (max %d)", tx_data_len, MAX_DATA_SIZE);
        free(json_response);
        return;
    }

    memcpy(tx_buffer, json_response, tx_data_len);
    tx_data_offset = 0;

    ESP_LOGI(TAG, "Prepared JSON-RPC response (%lu bytes): %s", tx_data_len, json_response);
    free(json_response);

    // Send RX_CTL notification with response length (4 bytes, big-endian)
    if (is_connected && gatt_if_global != ESP_GATT_IF_NONE) {
        uint8_t length_bytes[4];
        length_bytes[0] = (tx_data_len >> 24) & 0xFF;
        length_bytes[1] = (tx_data_len >> 16) & 0xFF;
        length_bytes[2] = (tx_data_len >> 8) & 0xFF;
        length_bytes[3] = tx_data_len & 0xFF;

        esp_err_t ret = esp_ble_gatts_send_indicate(gatt_if_global, conn_id_global,
                                                    attr_handle_table[IDX_CHAR_RX_CTL_VAL], 4, length_bytes, false);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Sent RX_CTL notification: response length = %lu", tx_data_len);
        } else {
            ESP_LOGE(TAG, "Failed to send RX_CTL notification: %s", esp_err_to_name(ret));
        }
    }
}

/**
 * @brief GAP event handler
 */
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            ESP_LOGI(TAG, "Advertising data set, starting advertising");
            esp_ble_gap_start_advertising(&adv_params);
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Advertising started successfully");
            } else {
                ESP_LOGE(TAG, "Advertising start failed: %d", param->adv_start_cmpl.status);
            }
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            ESP_LOGI(TAG, "Advertising stopped");
            break;
        default:
            break;
    }
}

/**
 * @brief GATTS event handler
 */
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
        case ESP_GATTS_REG_EVT:
            ESP_LOGI(TAG, "GATT server registered, status: %d, app_id: %d", param->reg.status, param->reg.app_id);

            gatt_if_global = gatts_if;

            // Set device name
            uint8_t mac[6];
            esp_read_mac(mac, ESP_MAC_BT);
            char device_name[32];
            snprintf(device_name, sizeof(device_name), "ShellyEric-%02X%02X%02X", mac[3], mac[4], mac[5]);
            esp_ble_gap_set_device_name(device_name);
            ESP_LOGI(TAG, "Device name set to: %s", device_name);

            // Configure advertising data
            esp_ble_gap_config_adv_data(&adv_data);

            // Create GATT service
            esp_gatt_srvc_id_t service_id = {
                .is_primary = true,
                .id =
                    {
                        .inst_id = 0,
                        .uuid =
                            {
                                .len = ESP_UUID_LEN_128,
                            },
                    },
            };
            memcpy(service_id.id.uuid.uuid.uuid128, SERVICE_UUID, 16);
            esp_ble_gatts_create_service(gatts_if, &service_id, 10);
            break;

        case ESP_GATTS_CREATE_EVT:
            ESP_LOGI(TAG, "Service created, handle: %d", param->create.service_handle);
            service_handle = param->create.service_handle;
            attr_handle_table[IDX_SVC] = service_handle;

            esp_ble_gatts_start_service(service_handle);

            // Add TX_CTL characteristic (Write)
            esp_bt_uuid_t char_tx_uuid = {
                .len = ESP_UUID_LEN_128,
            };
            memcpy(char_tx_uuid.uuid.uuid128, CHAR_TX_CTL_UUID, 16);

            esp_ble_gatts_add_char(service_handle, &char_tx_uuid, ESP_GATT_PERM_WRITE, ESP_GATT_CHAR_PROP_BIT_WRITE,
                                   NULL, NULL);
            break;

        case ESP_GATTS_ADD_CHAR_EVT:
            ESP_LOGI(TAG, "Characteristic added, handle: %d, uuid: 0x%02x (byte 6)", param->add_char.attr_handle,
                     param->add_char.char_uuid.uuid.uuid128[6]);

            // Determine which characteristic was added by checking byte 6 of UUID
            // TX_CTL has 0x74 ('t'), RX_CTL has 0x72 ('r'), DATA has 0x64 ('d')
            if (param->add_char.char_uuid.uuid.uuid128[6] == 0x74) {
                // TX_CTL added
                attr_handle_table[IDX_CHAR_TX_CTL_VAL] = param->add_char.attr_handle;
                char_tx_handle = param->add_char.attr_handle;

                // Add RX_CTL characteristic (Read/Notify)
                esp_bt_uuid_t char_rx_uuid = {
                    .len = ESP_UUID_LEN_128,
                };
                memcpy(char_rx_uuid.uuid.uuid128, CHAR_RX_CTL_UUID, 16);

                esp_ble_gatts_add_char(service_handle, &char_rx_uuid, ESP_GATT_PERM_READ,
                                       ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY, NULL, NULL);
            } else if (param->add_char.char_uuid.uuid.uuid128[6] == 0x72) {
                // RX_CTL added
                attr_handle_table[IDX_CHAR_RX_CTL_VAL] = param->add_char.attr_handle;
                char_rx_handle = param->add_char.attr_handle;

                // Add DATA characteristic (Read/Write)
                esp_bt_uuid_t char_data_uuid = {
                    .len = ESP_UUID_LEN_128,
                };
                memcpy(char_data_uuid.uuid.uuid128, CHAR_DATA_UUID, 16);

                esp_ble_gatts_add_char(service_handle, &char_data_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                       ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE, NULL, NULL);
            } else if (param->add_char.char_uuid.uuid.uuid128[6] == 0x64) {
                // DATA added
                attr_handle_table[IDX_CHAR_DATA_VAL] = param->add_char.attr_handle;
                char_data_handle = param->add_char.attr_handle;

                ESP_LOGI(TAG, "All characteristics added successfully");
            }
            break;

        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(TAG, "Client connected, conn_id: %d, remote: %02x:%02x:%02x:%02x:%02x:%02x",
                     param->connect.conn_id, param->connect.remote_bda[0], param->connect.remote_bda[1],
                     param->connect.remote_bda[2], param->connect.remote_bda[3], param->connect.remote_bda[4],
                     param->connect.remote_bda[5]);

            conn_id_global = param->connect.conn_id;
            is_connected = true;

            // Update connection parameters for better performance
            esp_ble_conn_update_params_t conn_params = {0};
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            conn_params.min_int = 0x10; // 20ms
            conn_params.max_int = 0x20; // 40ms
            conn_params.latency = 0;
            conn_params.timeout = 400; // 4s
            esp_ble_gap_update_conn_params(&conn_params);
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG, "Client disconnected, reason: %d", param->disconnect.reason);
            is_connected = false;
            conn_id_global = 0xFFFF;

            // Reset all state
            rx_expected_len = 0;
            rx_received_len = 0;
            tx_data_len = 0;
            tx_data_offset = 0;
            mtu_size = 23;

            // Restart advertising
            esp_ble_gap_start_advertising(&adv_params);
            break;

        case ESP_GATTS_WRITE_EVT:
            if (param->write.handle == attr_handle_table[IDX_CHAR_TX_CTL_VAL]) {
                // TX_CTL: Receive expected data length (4 bytes, big-endian)
                if (param->write.len == 4) {
                    rx_expected_len = ((uint32_t)param->write.value[0] << 24) |
                                      ((uint32_t)param->write.value[1] << 16) | ((uint32_t)param->write.value[2] << 8) |
                                      ((uint32_t)param->write.value[3]);

                    rx_received_len = 0; // Reset received counter

                    ESP_LOGI(TAG, "TX_CTL: Expecting %lu bytes of data", rx_expected_len);

                    if (rx_expected_len > MAX_DATA_SIZE) {
                        ESP_LOGE(TAG, "Expected data size too large: %lu bytes (max %d)", rx_expected_len,
                                 MAX_DATA_SIZE);
                        rx_expected_len = 0;
                    }
                } else {
                    ESP_LOGW(TAG, "TX_CTL: Invalid length %d (expected 4 bytes)", param->write.len);
                }

                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK,
                                                NULL);
                }
            } else if (param->write.handle == attr_handle_table[IDX_CHAR_DATA_VAL]) {
                // DATA: Receive data chunks
                ESP_LOGI(TAG, "DATA write: %d bytes (total: %lu/%lu)", param->write.len,
                         rx_received_len + param->write.len, rx_expected_len);

                if (rx_expected_len == 0) {
                    ESP_LOGW(TAG, "DATA write before TX_CTL, ignoring");
                } else if (rx_received_len + param->write.len <= rx_expected_len) {
                    memcpy(rx_buffer + rx_received_len, param->write.value, param->write.len);
                    rx_received_len += param->write.len;

                    // Check if complete request received
                    if (rx_received_len >= rx_expected_len) {
                        ESP_LOGI(TAG, "Complete request received (%lu bytes)", rx_received_len);
                        process_request();

                        // Reset for next request
                        rx_expected_len = 0;
                        rx_received_len = 0;
                    }
                } else {
                    ESP_LOGE(TAG, "DATA overflow: %d bytes exceeds expected %lu", param->write.len,
                             rx_expected_len - rx_received_len);
                }

                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK,
                                                NULL);
                }
            }
            break;

        case ESP_GATTS_READ_EVT:
            ESP_LOGI(TAG, "Read event, handle: %d", param->read.handle);

            esp_gatt_rsp_t rsp;
            memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
            rsp.attr_value.handle = param->read.handle;

            if (param->read.handle == attr_handle_table[IDX_CHAR_DATA_VAL]) {
                // DATA read: Send response data chunk
                if (tx_data_offset < tx_data_len) {
                    uint16_t chunk_size = (tx_data_len - tx_data_offset);
                    if (chunk_size > (mtu_size - 3)) {
                        chunk_size = mtu_size - 3; // Leave room for ATT overhead
                    }

                    memcpy(rsp.attr_value.value, tx_buffer + tx_data_offset, chunk_size);
                    rsp.attr_value.len = chunk_size;
                    tx_data_offset += chunk_size;

                    ESP_LOGI(TAG, "DATA read: Sent %d bytes (offset: %lu/%lu)", chunk_size, tx_data_offset,
                             tx_data_len);

                    // Reset when complete
                    if (tx_data_offset >= tx_data_len) {
                        ESP_LOGI(TAG, "Response transmission complete");
                        tx_data_len = 0;
                        tx_data_offset = 0;
                    }
                } else {
                    ESP_LOGW(TAG, "DATA read: No data available");
                    rsp.attr_value.len = 0;
                }
            } else if (param->read.handle == attr_handle_table[IDX_CHAR_RX_CTL_VAL]) {
                // RX_CTL read: Return response length (4 bytes, big-endian)
                rsp.attr_value.value[0] = (tx_data_len >> 24) & 0xFF;
                rsp.attr_value.value[1] = (tx_data_len >> 16) & 0xFF;
                rsp.attr_value.value[2] = (tx_data_len >> 8) & 0xFF;
                rsp.attr_value.value[3] = tx_data_len & 0xFF;
                rsp.attr_value.len = 4;

                ESP_LOGI(TAG, "RX_CTL read: Response length = %lu", tx_data_len);
            } else {
                rsp.attr_value.len = 0;
            }

            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
            break;

        case ESP_GATTS_MTU_EVT:
            ESP_LOGI(TAG, "MTU negotiated: %d", param->mtu.mtu);
            mtu_size = param->mtu.mtu;
            break;

        case ESP_GATTS_START_EVT:
            ESP_LOGI(TAG, "Service started");
            break;

        default:
            break;
    }
}

/**
 * @brief Initialize BLE server
 */
esp_err_t ble_server_init(void) {
    esp_err_t ret;

    // Initialize BT controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize Bluedroid
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register GAP and GATTS callbacks
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP callback register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTS callback register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register GATT application
    ret = esp_ble_gatts_app_register(0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTS app register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set MTU
    ret = esp_ble_gatt_set_local_mtu(512);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Set local MTU failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "BLE GATT Server initialized successfully");
    return ESP_OK;
}

/**
 * @brief Check if BLE client is connected
 */
bool ble_server_is_connected(void) {
    return is_connected;
}

/**
 * @brief Get current MTU size
 */
uint16_t ble_server_get_mtu(void) {
    return mtu_size;
}
