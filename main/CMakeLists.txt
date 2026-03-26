file(GLOB MAIN_SRCS "rpc_m_wifi.c" "rpc_m_ble.c" "*.c" "*.cpp")

idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS "."
)
