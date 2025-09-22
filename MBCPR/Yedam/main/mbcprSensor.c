    #include <stdio.h>
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h" 
    #include "esp_log.h"
    #include "driver/i2c.h"
    #include "cap1188.h"

    #define pdMS_TO_TICKS(xTimeInMs) ( ( TickType_t ) ( ( ( TickType_t ) ( xTimeInMs ) * ( TickType_t ) configTICK_RATE_HZ ) / ( TickType_t ) 1000U ) )

    #define MPU6050_SENSOR_ADDR         0x68        
    
    const char *TAG = "MPU6050";   

    uint8_t CapValue = 0;
    int16_t MpuValueX = 0, mpuValueY = 0, mpuValueZ = 0;

    bool finished = false;

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

                MpuValueX = acc_x, mpuValueY = acc_y, mpuValueZ = acc_z;
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
        cap1188_init();
        uint8_t touched;

        while(!finished){
            cap1188_read(0x03, &touched); //터치 상태 레지스터 읽기
            if(touched){
                for(uint8_t i = 0; i < 8; i++){
                    if(touched & (1 << i)){ //각 비트 확인(그부분 비트만 확인해서 터치 됐는지 안됐는지)
                        CapValue = i + 1;
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        vTaskDelete(NULL);
    }


    //메인
    void app_main(void)
    {
        i2c_master_init();
        
        uint8_t reg = 0x6B;      // Power Management 1
        uint8_t data = 0x00;     // Wake up
        i2c_master_write_slave(I2C_MASTER_NUM, MPU6050_SENSOR_ADDR, &reg, 1); // 레지스터 선택
        i2c_master_write_slave(I2C_MASTER_NUM, MPU6050_SENSOR_ADDR, &data, 1); // 값 쓰기

        //기울기 센서
        xTaskCreate(mpu6050_task, "mpu6050_task", 4096, NULL, 5, NULL);
        //터치센서
        xTaskCreate(cap1188_task, "cap1188_task", 4096, NULL, 5, NULL);
        
    }
