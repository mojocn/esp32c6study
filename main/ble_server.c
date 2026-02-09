#include "ble_server.h"
#include "jsonrpc.h"
#include "config.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "BLE";

/* BLE Manufacturer Data - ALLTERCO */
static uint8_t manufacturer_data[] = {
    0xA9, 0x0B /* Company ID: 0x0BA9 (ALLTERCO) in little-endian */
};

/* BLE Service and Characteristic UUIDs (128-bit) */
static uint8_t service_uuid[16] = {
    /* "5f6d4f53-5f52-5043-5f53-56435f49445f" */
    0x5f, 0x44, 0x49, 0x5f, 0x43, 0x56, 0x53, 0x5f,
    0x43, 0x50, 0x52, 0x5f, 0x53, 0x4f, 0x6d, 0x5f};

static uint8_t char_tx_ctl_uuid[16] = {
    /* "5f6d4f53-5f52-5043-5f74-785f63746c5f" */
    0x5f, 0x6c, 0x74, 0x63, 0x5f, 0x78, 0x74, 0x5f,
    0x43, 0x50, 0x52, 0x5f, 0x53, 0x4f, 0x6d, 0x5f};

static uint8_t char_data_uuid[16] = {
    /* "5f6d4f53-5f52-5043-5f64-6174615f5f5f" */
    0x5f, 0x5f, 0x5f, 0x61, 0x74, 0x61, 0x64, 0x5f,
    0x43, 0x50, 0x52, 0x5f, 0x53, 0x4f, 0x6d, 0x5f};

static uint8_t char_rx_ctl_uuid[16] = {
    /* "5f6d4f53-5f52-5043-5f72-785f63746c5f" */
    0x5f, 0x6c, 0x74, 0x63, 0x5f, 0x78, 0x72, 0x5f,
    0x43, 0x50, 0x52, 0x5f, 0x53, 0x4f, 0x6d, 0x5f};

/* BLE GATT Server Profile */
struct gatts_profile_inst
{
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle_tx_ctl;
    uint16_t char_handle_data;
    uint16_t char_handle_rx_ctl;
    uint16_t descr_handle_rx_ctl;
};

static struct gatts_profile_inst gl_profile;
static uint16_t ble_conn_id = 0xFFFF;
static esp_gatt_if_t ble_gatts_if = 0xFF;
static bool ble_connected = false;

/* BLE RPC Buffer */
static char ble_rpc_buffer[BLE_RPC_BUFFER_SIZE];
static uint16_t ble_rpc_buffer_len = 0;

/* BLE: Send response via notification */
static void ble_send_response(const char *response)
{
    if (!ble_connected || response == NULL)
    {
        return;
    }

    uint16_t response_len = strlen(response);
    uint16_t offset = 0;
    uint16_t mtu = BLE_MTU_SIZE - 3; // Account for ATT overhead

    ESP_LOGI(TAG, "BLE Sending response: %s", response);

    while (offset < response_len)
    {
        uint16_t chunk_len = (response_len - offset) > mtu ? mtu : (response_len - offset);
        esp_ble_gatts_send_indicate(ble_gatts_if, ble_conn_id,
                                    gl_profile.char_handle_rx_ctl,
                                    chunk_len, (uint8_t *)(response + offset), false);
        offset += chunk_len;
        vTaskDelay(pdMS_TO_TICKS(20)); // Small delay between chunks
    }
}

/* BLE: Process received data */
static void ble_process_rpc_request(void)
{
    if (ble_rpc_buffer_len == 0)
    {
        return;
    }

    ble_rpc_buffer[ble_rpc_buffer_len] = '\0';
    ESP_LOGI(TAG, "BLE Received: %s", ble_rpc_buffer);

    /* Process JSON-RPC request */
    char *response = jsonrpc_process_request(ble_rpc_buffer);

    if (response != NULL)
    {
        ble_send_response(response);
        free(response);
    }

    /* Clear buffer */
    ble_rpc_buffer_len = 0;
    memset(ble_rpc_buffer, 0, BLE_RPC_BUFFER_SIZE);
}

/* BLE GAP Event Handler */
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&(esp_ble_adv_params_t){
            .adv_int_min = 0x20,
            .adv_int_max = 0x40,
            .adv_type = ADV_TYPE_IND,
            .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
            .channel_map = ADV_CHNL_ALL,
            .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        });
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGI(TAG, "BLE Advertising started");
        }
        break;
    default:
        break;
    }
}

/* BLE GATT Server Event Handler */
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(TAG, "BLE GATT Server registered, app_id=%d", param->reg.app_id);
        gl_profile.gatts_if = gatts_if;

        /* Set device name */
        esp_ble_gap_set_device_name(BLE_DEVICE_NAME);

        /* Configure advertising data */
        esp_ble_adv_data_t adv_data = {
            .set_scan_rsp = false,
            .include_name = true,
            .include_txpower = true,
            .min_interval = 0x0006,
            .max_interval = 0x0010,
            .appearance = 0x00,
            .manufacturer_len = sizeof(manufacturer_data),
            .p_manufacturer_data = manufacturer_data,
            .service_data_len = 0,
            .p_service_data = NULL,
            .service_uuid_len = sizeof(service_uuid),
            .p_service_uuid = service_uuid,
            .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
        };
        esp_ble_gap_config_adv_data(&adv_data);

        /* Create service */
        esp_ble_gatts_create_service(gatts_if, &(esp_gatt_srvc_id_t){
                                                   .is_primary = true,
                                                   .id = {
                                                       .inst_id = 0,
                                                       .uuid = {
                                                           .len = ESP_UUID_LEN_128,
                                                           .uuid = {.uuid128 = {0}},
                                                       },
                                                   },
                                               },
                                     GATTS_NUM_HANDLE);
        memcpy(((esp_gatt_srvc_id_t *)&param->create.service_id)->id.uuid.uuid.uuid128, service_uuid, 16);
        break;

    case ESP_GATTS_CREATE_EVT:
        ESP_LOGI(TAG, "BLE Service created, handle=%d", param->create.service_handle);
        gl_profile.service_handle = param->create.service_handle;

        /* Start service */
        esp_ble_gatts_start_service(gl_profile.service_handle);

        /* Add TX_CTL characteristic (Write) */
        esp_ble_gatts_add_char(gl_profile.service_handle, &(esp_bt_uuid_t){
                                                              .len = ESP_UUID_LEN_128,
                                                              .uuid.uuid128 = {0},
                                                          },
                               ESP_GATT_PERM_WRITE, ESP_GATT_CHAR_PROP_BIT_WRITE, &(esp_attr_value_t){.attr_max_len = BLE_MTU_SIZE, .attr_len = 0, .attr_value = NULL}, NULL);
        memcpy(((esp_bt_uuid_t *)&param->add_char.char_uuid)->uuid.uuid128, char_tx_ctl_uuid, 16);
        break;

    case ESP_GATTS_ADD_CHAR_EVT:
        ESP_LOGI(TAG, "BLE Characteristic added, handle=%d", param->add_char.attr_handle);

        if (gl_profile.char_handle_tx_ctl == 0)
        {
            gl_profile.char_handle_tx_ctl = param->add_char.attr_handle;
            /* Add DATA characteristic (Read/Write) */
            esp_ble_gatts_add_char(gl_profile.service_handle, &(esp_bt_uuid_t){
                                                                  .len = ESP_UUID_LEN_128,
                                                                  .uuid.uuid128 = {0},
                                                              },
                                   ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE, &(esp_attr_value_t){.attr_max_len = BLE_MTU_SIZE, .attr_len = 0, .attr_value = NULL}, NULL);
            memcpy(((esp_bt_uuid_t *)&param->add_char.char_uuid)->uuid.uuid128, char_data_uuid, 16);
        }
        else if (gl_profile.char_handle_data == 0)
        {
            gl_profile.char_handle_data = param->add_char.attr_handle;
            /* Add RX_CTL characteristic (Read/Notify) */
            esp_ble_gatts_add_char(gl_profile.service_handle, &(esp_bt_uuid_t){
                                                                  .len = ESP_UUID_LEN_128,
                                                                  .uuid.uuid128 = {0},
                                                              },
                                   ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY, &(esp_attr_value_t){.attr_max_len = BLE_MTU_SIZE, .attr_len = 0, .attr_value = NULL}, NULL);
            memcpy(((esp_bt_uuid_t *)&param->add_char.char_uuid)->uuid.uuid128, char_rx_ctl_uuid, 16);
        }
        else if (gl_profile.char_handle_rx_ctl == 0)
        {
            gl_profile.char_handle_rx_ctl = param->add_char.attr_handle;
        }
        break;

    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(TAG, "BLE Client connected, conn_id=%d", param->connect.conn_id);
        ble_conn_id = param->connect.conn_id;
        ble_gatts_if = gatts_if;
        ble_connected = true;
        gl_profile.conn_id = param->connect.conn_id;

        /* Update connection parameters */
        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        conn_params.min_int = 0x10;
        conn_params.max_int = 0x20;
        conn_params.latency = 0;
        conn_params.timeout = 400;
        esp_ble_gap_update_conn_params(&conn_params);
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG, "BLE Client disconnected");
        ble_connected = false;
        ble_rpc_buffer_len = 0;
        esp_ble_gap_start_advertising(&(esp_ble_adv_params_t){
            .adv_int_min = 0x20,
            .adv_int_max = 0x40,
            .adv_type = ADV_TYPE_IND,
            .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
            .channel_map = ADV_CHNL_ALL,
            .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        });
        break;

    case ESP_GATTS_WRITE_EVT:
        if (param->write.handle == gl_profile.char_handle_tx_ctl ||
            param->write.handle == gl_profile.char_handle_data)
        {
            /* Append received data to buffer */
            if (ble_rpc_buffer_len + param->write.len < BLE_RPC_BUFFER_SIZE)
            {
                memcpy(ble_rpc_buffer + ble_rpc_buffer_len, param->write.value, param->write.len);
                ble_rpc_buffer_len += param->write.len;

                /* Check if we have a complete JSON object */
                if (param->write.value[param->write.len - 1] == '}' ||
                    param->write.value[param->write.len - 1] == '\n')
                {
                    ble_process_rpc_request();
                }
            }
            else
            {
                ESP_LOGW(TAG, "BLE RPC buffer overflow");
                ble_rpc_buffer_len = 0;
            }
        }

        if (param->write.need_rsp)
        {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        }
        break;

    case ESP_GATTS_READ_EVT:
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_OK, &(esp_gatt_rsp_t){
                                                     .attr_value.len = 0,
                                                     .attr_value.value[0] = 0,
                                                 });
        break;

    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(TAG, "BLE MTU exchanged, MTU=%d", param->mtu.mtu);
        break;

    default:
        break;
    }
}

esp_err_t ble_server_init(void)
{
    esp_err_t ret;

    /* Release classic BT memory */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret)
    {
        ESP_LOGE(TAG, "BLE controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret)
    {
        ESP_LOGE(TAG, "BLE controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret)
    {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret)
    {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register callbacks */
    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gap_register_callback(gap_event_handler);

    /* Register application */
    esp_ble_gatts_app_register(PROFILE_APP_ID);

    /* Set MTU */
    esp_ble_gatt_set_local_mtu(BLE_MTU_SIZE);

    ESP_LOGI(TAG, "BLE initialized");
    return ESP_OK;
}
