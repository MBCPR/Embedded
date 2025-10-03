idf_component_register(
    SRCS "mbcprSensor.c" "cap1188.c"
    INCLUDE_DIRS "."
    REQUIRES "esp_websocket_client" "esp_wifi" "nvs_flash" "esp_event" "driver" "freertos" "log"
)