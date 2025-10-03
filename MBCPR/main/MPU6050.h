/*
// 임시로 사용되지 않으므로 전체 코드 주석 처리됨
#ifndef MPU6050_H_
#define MPU6050_H_

#include "esp_err.h"
#include <stdint.h>

#define MPU6050_SENSOR_ADDR         0x68
// 가속도계 데이터 레지스터 상수 추가
#define MPU6050_REG_ACCEL_XOUT_H    0x3B 

void mpu6050_init();
esp_err_t mpu6050_read_accel(int16_t *acc_x, int16_t *acc_y, int16_t *acc_z);

#endif // MPU6050_H_
*/