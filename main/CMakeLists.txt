idf_component_register(SRCS  "store.c" "main.c"
                            "gpio_control.c"
                            "jsonrpc.c"
                            "jsonrpc_methods.c"
                            "http_server.c"
                            "wifi_manager.c"

                            "mqtt_manager.c"
                            "ota_manager.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash esp_wifi esp_netif esp_http_server driver json bt wifi_provisioning protocomm led_strip mqtt app_update esp_https_ota)
