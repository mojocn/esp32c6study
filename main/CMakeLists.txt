file(GLOB MAIN_SRCS "*.c" "*.cpp")

idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS "."
)
