idf_component_register(
    SRCS
    "globalcache_server.cpp"
    "globalcache.cpp"
    "ir_codes.cpp"
    "service_ir.cpp"
    INCLUDE_DIRS
    "."
    REQUIRES
    common
    external_port
    preferences
    IRremoteESP8266
    log
    json
)
 