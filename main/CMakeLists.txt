idf_component_register(SRCS "main.c" 
                            "gpio_control.c" 
                            "jsonrpc.c" 
                            "http_server.c" 
                            "ble_server.c" 
                            "wifi_manager.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash esp_wifi esp_netif esp_http_server driver json bt wifi_provisioning protocomm led_strip)
