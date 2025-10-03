# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.


idf_component_register(
	SRCS "main.c" "HX711.c" "i2c_master.c" "WIFI.c" "wifi_nvs_manager.c" "websocket_client.c" "URL.c" "MPU6050.c" "cap1188.c"
    
	REQUIRES driver esp_http_server esp_websocket_client nvs_flash freertos lwip log esp_system esp_event esp_netif esp_wifi

	PRIV_INCLUDE_DIRS . 
)