#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "driver/i2c.h"
// #include "cap1188.h"
#include "websocket_client.h"

#define pdMS_TO_TICKS(xTimeInMs) ( ( TickType_t ) ( ( ( TickType_t ) ( xTimeInMs ) * ( TickType_t ) configTICK_RATE_HZ ) / ( TickType_t ) 1000U ) )
#define MPU6050_SENSOR_ADDR         0x68

esp_websocket_client_handle_t client = NULL;
int64_t last_connect_time = 0;

const char *TAG = "MPU6050";
const char *WS = "ws_client";
const char *CAP = "cap1188";
const char *WIFI = "wifi";

//uint8_t CapValue = 0;
int16_t mpuValueX = 0, mpuValueY = 0, mpuValueZ = 0;
int HX711Value = 0;

bool finished = true; //데이터 전송 종료 플래그
    
//기울기 센서 태스크
void mpu6050_task(void *arg){
    uint8_t data[6];

    uint8_t reg = 0x3B; // ACCEL_XOUT_H

    while(!finished){
        if(i2c_master_write_slave(I2C_MASTER_NUM, MPU6050_SENSOR_ADDR, &reg, 1) != ESP_OK){ //레지스터 선택
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
// void cap1188_task(void* arg){
//     uint8_t touched;
//     uint8_t clear_int_command[2] = {0x00, 0x01}; // 인터럽트 클리어 값
//     while(!finished){
//         cap1188_read(0x03, &touched); //터치 상태 레지스터 읽기
//         CapValue = touched;
//         if(touched){
//             for(uint8_t i = 0; i < 8; i++){
//                 if(touched & (1 << i)){ //각 비트 확인(그부분 비트만 확인해서 터치 됐는지 안됐는지)
//                     ESP_LOGE(CAP, "touched: %d", i + 1);
//                 }
//             }
//             if(i2c_master_write_slave(I2C_MASTER_NUM, ESP_ADDR_CAP1188, clear_int_command, 2) != ESP_OK){
//                 ESP_LOGE(CAP, "Failed to clear interrupt");
//             }
//         }
//         vTaskDelay(pdMS_TO_TICKS(100));
//     }
//     vTaskDelete(NULL);
// }


//메인
void app_main(void)
{
    esp_event_loop_create_default(); //이벤트 루프 생성
    // cap1188_init();
    
    i2c_master_init();
    last_connect_time = esp_log_timestamp();

    //MPU6050 레지스터 전부 초기화
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
    // xTaskCreate(cap1188_task, "cap1188_task", 4096, NULL, 5, NULL);
    //웹소켓 통신
    xTaskCreate(start_websocket_client, "websocket_client", 8192, NULL, 5, NULL);
    //서버에서 메시지 오는지 확인
    xTaskCreate(check_server_message, "check_server_message", 2048, NULL, 5, NULL);
}
