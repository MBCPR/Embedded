        #include <stdio.h>
        #include "freertos/FreeRTOS.h"
        #include "freertos/task.h"
        #include "esp_log.h"
        #include "esp_websocket_client.h"
        #include "esp_event.h"
        #include "nvs_flash.h"
        #include "esp_wifi.h"
        #include "esp_netif.h"
        #include "driver/i2c.h"
        #include "cap1188.h"
        #include "websocket_client.h"
        #include "freertos/event_groups.h"

        #define pdMS_TO_TICKS(xTimeInMs) ( ( TickType_t ) ( ( ( TickType_t ) ( xTimeInMs ) * ( TickType_t ) configTICK_RATE_HZ ) / ( TickType_t ) 1000U ) )
        #define MPU6050_SENSOR_ADDR         0x68
        #define WIFI_SSID                 "추가예정"
        #define WIFI_PASS                 "추가예정"      

        esp_websocket_client_handle_t client = NULL;
        int64_t last_connect_time = 0;
        static EventGroupHandle_t wifi_event_group;
        const int WIFI_CONNECTED_BIT = BIT0;

        const char *TAG = "MPU6050";
        const char *WS = "ws_client";
        const char *CAP = "cap1188";
        const char *WIFI = "wifi";

        uint8_t CapValue = 0;
        int16_t mpuValueX = 0, mpuValueY = 0, mpuValueZ = 0;

        bool finished = false;

        //wifi 이벤트 핸들러
        static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data) {
            if (event_id == WIFI_EVENT_STA_START) {
                esp_wifi_connect();
            } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
                esp_wifi_connect();
                ESP_LOGI(WIFI, "재연결 시도 중...");
            } else if (event_id == IP_EVENT_STA_GOT_IP) {
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                ESP_LOGI(WIFI, "IP 주소 획득: " IPSTR, IP2STR(&event->ip_info.ip));

                if(client != NULL && !esp_websocket_client_is_connected(client)){
                    esp_websocket_client_start(client);
                    ESP_LOGI(WS, "웹소켓 재연결 시도중...");
                }
                xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
            }
        }

        //wifi 초기화
        void wifi_init() {
            esp_netif_init();
            esp_event_loop_create_default();
            esp_netif_create_default_wifi_sta();
            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            esp_wifi_init(&cfg);
            esp_event_handler_instance_t instance_any_id;
            esp_event_handler_instance_t instance_got_ip;
            esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);
            esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip);
            wifi_config_t wifi_config = {
                .sta = {
                    .ssid = WIFI_SSID,
                    .password = WIFI_PASS,
                    .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                },
            };
            esp_wifi_set_mode(WIFI_MODE_STA);
            esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
            esp_wifi_start();
            ESP_LOGI(WIFI, "WiFi 초기화 완료");
        }
           
        //기울기 센서 태스크
        void mpu6050_task(void *arg){
            uint8_t data[6];

            uint8_t reg = 0x3B; // ACCEL_XOUT_H

            while(!finished){
                if(i2c_master_write_slave(I2C_MASTER_NUM, MPU6050_SENSOR_ADDR, &reg, 1) != ESP_OK){
                    ESP_LOGE(TAG, "Failed to select register");
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                }
                if(i2c_master_read_slave(I2C_MASTER_NUM, MPU6050_SENSOR_ADDR, data, 6) == ESP_OK){
                    int16_t acc_x = (data[0] << 8) | data[1]; //mpu6050가 2바이트로 데이터 줘서 16비트로 합치는 과정(빅엔디안)
                    int16_t acc_y = (data[2] << 8) | data[3];
                    int16_t acc_z = (data[4] << 8) | data[5];

                    ESP_LOGE(TAG, "read: X=%d, Y=%d, Z=%d", acc_x, acc_y, acc_z);
                    mpuValueX = acc_x;
                    mpuValueY = acc_y;
                    mpuValueZ = acc_z;
                }
                else {
                    ESP_LOGE(TAG, "MPU6050 read error");
                }

                vTaskDelay(pdMS_TO_TICKS(100));
            }
            vTaskDelete(NULL);
        }

        //터치센서 태스크
        void cap1188_task(void* arg){
            uint8_t touched;
            uint8_t clear_int_command[2] = {0x00, 0x01}; // 인터럽트 클리어 값

            while(!finished){
                cap1188_read(0x03, &touched); //터치 상태 레지스터 읽기
                CapValue = touched;
                if(touched){
                    for(uint8_t i = 0; i < 8; i++){
                        if(touched & (1 << i)){ //각 비트 확인(그부분 비트만 확인해서 터치 됐는지 안됐는지)
                            ESP_LOGE(CAP, "touched: %d", i + 1);
                        }
                    }
                    if(i2c_master_write_slave(I2C_MASTER_NUM, ESP_ADDR_CAP1188, clear_int_command, 2) != ESP_OK){
                        ESP_LOGE(CAP, "Failed to clear interrupt");
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            vTaskDelete(NULL);
        }


        //메인
        void app_main(void)
        {
            nvs_flash_init();
            esp_netif_init();
            esp_event_loop_create_default();
            cap1188_init();

            wifi_event_group = xEventGroupCreate();
            wifi_init();

            ESP_LOGI(TAG, "WIFI 연결 대기 중...");
            xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

            ESP_LOGI(TAG, "WIFI 연결됨");
            
            i2c_master_init();
            last_connect_time = esp_log_timestamp();

            //MPU6050 초기화
            uint8_t data[2];

            data[0] = 0x6B; // 레지스터 주소
            data[1] = 0x00; // 슬립 모드 해제
            i2c_master_write_slave(I2C_MASTER_NUM, MPU6050_SENSOR_ADDR, data, 2); // 값 쓰기

            data[0] = 0x19; // Sample Rate Divider
            data[1] = 0x07; // 1kHz/(1+7) = 125Hz
            i2c_master_write_slave(I2C_MASTER_NUM, MPU6050_SENSOR_ADDR, data, 2); // 값 쓰기

            data[0] = 0x1B; // Gyroscope Configuration
            data[1] = 0x00; // ±250°/s
            i2c_master_write_slave(I2C_MASTER_NUM, MPU6050_SENSOR_ADDR, data, 2); // 값 쓰기

            data[0] = 0x1C; // Accelerometer Configuration
            data[1] = 0x00; // ±2g
            i2c_master_write_slave(I2C_MASTER_NUM, MPU6050_SENSOR_ADDR, data, 2); // 값 쓰기

            //기울기 센서
            xTaskCreate(mpu6050_task, "mpu6050_task", 4096, NULL, 5, NULL);
            //터치센서
            xTaskCreate(cap1188_task, "cap1188_task", 4096, NULL, 5, NULL);
            //웹소켓 통신
            xTaskCreate(start_websocket_client, "websocket_client", 8192, NULL, 5, NULL);
            //서버에서 메시지 오는지 확인
            xTaskCreate(check_server_message, "check_server_message", 2048, NULL, 5, NULL);
        }
