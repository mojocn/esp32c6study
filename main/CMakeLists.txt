idf_component_register(SRCS "store.c" "main.c"
                            "gpio_control.c"
                            "jsonrpc.c"
                            "jsonrpc_methods.c"
                            "http_server.c"
                            "wifi_manager.c"
                            "ble_server.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash esp_wifi esp_netif esp_http_server driver json bt wifi_provisioning protocomm led_strip)
