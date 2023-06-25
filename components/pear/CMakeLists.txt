
file(GLOB CODES "pear/src/*.c")

idf_component_register(
    SRCS ${CODES}
    INCLUDE_DIRS "pear/src/"
    REQUIRES mbedtls srtp mdns
)

add_compile_definitions(ESP32)

