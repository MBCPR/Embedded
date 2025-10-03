/*
// MPU6050 센서가 임시로 사용되지 않으므로 전체 코드 주석 처리됨
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "MPU6050.h" 
#include "i2c_master.h"

const char *TAG = "MPU6050";

void mpu6050_init(void) {
    uint8_t data[2];

    // 1. 전원 관리 레지스터(0x6B) 설정: 슬립 모드 해제
    data[0] = 0x6B;       
    data[1] = 0x00;      
    i2c_master_write_slave(I2C_MASTER_NUM, MPU6050_SENSOR_ADDR, data, 2); 

    // 2. 샘플링 속도 분배기(0x19) 설정: 1kHz / (1 + 7) = 125Hz
    data[0] = 0x19; 
    data[1] = 0x07; 
    i2c_master_write_slave(I2C_MASTER_NUM, MPU6050_SENSOR_ADDR, data, 2); 

    // 3. 자이로스코프 설정(0x1B): ±250°/s
    data[0] = 0x1B; 
    data[1] = 0x00; 
    i2c_master_write_slave(I2C_MASTER_NUM, MPU6050_SENSOR_ADDR, data, 2);  

    // 4. 가속도계 설정(0x1C): ±2g
    data[0] = 0x1C; 
    data[1] = 0x00; 
    i2c_master_write_slave(I2C_MASTER_NUM, MPU6050_SENSOR_ADDR, data, 2);  
}

// MPU6050 데이터 읽기 로직을 별도 함수로 분리 (반복 시작 사용)
esp_err_t mpu6050_read_accel(int16_t *acc_x, int16_t *acc_y, int16_t *acc_z) {
    uint8_t data[6];

    // MPU6050_REG_ACCEL_XOUT_H (0x3B)부터 6바이트 읽기 (반복 시작 활용)
    esp_err_t ret = i2c_master_read_reg_slave(I2C_MASTER_NUM, MPU6050_SENSOR_ADDR, MPU6050_REG_ACCEL_XOUT_H, data, 6);

    if (ret == ESP_OK) {
        // 빅 엔디안으로 16비트 데이터 조합
        *acc_x = (data[0] << 8) | data[1]; 
        *acc_y = (data[2] << 8) | data[3];
        *acc_z = (data[4] << 8) | data[5];
    }

    return ret;
}
*/