#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h" 
#include "esp_log.h"
#include "driver/i2c.h"
#include "cap1188.h"

#define pdMS_TO_TICKS(xTimeInMs) ( ( TickType_t ) ( ( ( TickType_t ) ( xTimeInMs ) * ( TickType_t ) configTICK_RATE_HZ ) / ( TickType_t ) 1000U ) )

#define I2C_MASTER_SCL_IO           22          
#define I2C_MASTER_SDA_IO           21          
#define I2C_MASTER_NUM              I2C_NUM_0   
#define I2C_MASTER_FREQ_HZ          100000     
#define I2C_MASTER_TX_BUF_DISABLE   0           
#define I2C_MASTER_RX_BUF_DISABLE   0

#define MPU6050_SENSOR_ADDR         0x68        
#define MPU6050_WHO_AM_I_REG        0x75        
#define MPU6050_WHO_AM_I_VAL        0x68        

const char *TAG = "MPU6050";   

 //초기화
void i2c_master_init()
{
    int i2c_master_port = I2C_MASTER_NUM;
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = I2C_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    i2c_param_config(i2c_master_port, &conf);
    i2c_driver_install(i2c_master_port, conf.mode,
                       I2C_MASTER_RX_BUF_DISABLE,
                       I2C_MASTER_TX_BUF_DISABLE, 0);
}

//마스터로 슬레이브에서 데이터 읽는 코드
esp_err_t i2c_master_read_slave(i2c_port_t i2c_num, uint8_t slave_addr,
                              uint8_t *data_rd, size_t size)
{
    if (size == 0) { //읽을거 없으면 ㅇㅋ
        return ESP_OK;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (slave_addr << 1) | I2C_MASTER_READ, true); //읽기모드 설정하고 ACK 체크 
    if (size > 1) { //1바이트 이상이면 -1 먼저읽고 ACK 전송
        i2c_master_read(cmd, data_rd, size - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data_rd + size - 1, I2C_MASTER_NACK); //마지막은 NACK 전송
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret;
}

//쓰는 코드
esp_err_t i2c_master_write_slave(i2c_port_t i2c_num, uint8_t slave_addr,
                               uint8_t *data_wr, size_t size)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (slave_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data_wr, size, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret;
}

//깅루기 센서 태스크
void mpu6050_task(void *arg){
     uint8_t data[6];

    while(1){
        if(i2c_master_read_slave(I2C_MASTER_NUM, MPU6050_SENSOR_ADDR, data, 6) == ESP_OK){
            int16_t acc_x = (data[0] << 8) | data[1]; //mpu6050가 2바이트로 데이터 줘서 16비트로 합치는 과정(빅엔디안)
            int16_t acc_y = (data[2] << 8) | data[3];
            int16_t acc_z = (data[4] << 8) | data[5];

            ESP_LOGI(TAG, "MPU6050 X: %d, Y: %d, Z: %d", acc_x, acc_y, acc_z);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

//터치센서 태스크
void cap1188_task(void* arg){
    cap1188_init();
    uint8_t touched;

    while(1){
        cap1188_read(0x03, &touched); //터치 상태 레지스터 읽기
        if(touched){
            for(uint8_t i = 0; i < 8; i++){
                if(touched & (1 << i)){ //각 비트 확인(그부분 비트만 확인해서 터치 됐는지 안됐는지)
                    ESP_LOGI("CAP1188", "Sensor %d touched", i + 1);
                }
            }
            printf("\n");
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}


//메인
void app_main(void)
{
    i2c_master_init();
    
    uint8_t wake_cmd[1] = {0x00};
    i2c_master_write_slave(MPU6050_SENSOR_ADDR, 0x6B, wake_cmd, 1);

    //기울기 센서
    xTaskCreate(mpu6050_task, "mpu6050_task", 4096, NULL, 5, NULL);
    //터치센서
    xTaskCreate(cap1188_task, "cap1188_task", 4096, NULL, 5, NULL);
    
}
