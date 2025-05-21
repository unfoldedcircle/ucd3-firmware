set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

idf_component_register(
    SRCS
    "main.cpp"
    "ota.cpp"
    "button.cpp"
    "charger.cpp"
    "ucd_api.cpp"
    INCLUDE_DIRS
     "."
    REQUIRES
    esp-tls
    app_update
    frogfs
    littlefs
    vfs
    log
    common
    display
    external_port
    infrared
    network
    preferences
    webserver
)
