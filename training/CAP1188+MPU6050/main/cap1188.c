#include "cap1188.h"
#include "driver/i2c.h"

#define I2C_MASTER_NUM I2C_NUM_0
#define ESP_ADDR_CAP1188 0x29

esp_err_t cap1188_write(uint8_t reg_addr, uint8_t *data){
    uint8_t write_buf[2];
    write_buf[0] = reg_addr; //레지스터 주소
    write_buf[1] = *data;    //쓸 데이터

    esp_err_t ret;
    ret = i2c_master_write_slave(I2C_MASTER_NUM, ESP_ADDR_CAP1188, write_buf, 2);
    return ret;
}

esp_err_t cap1188_read(uint8_t reg_addr, uint8_t *data)
{
    esp_err_t ret;
    ret = i2c_master_write_slave(I2C_MASTER_NUM, ESP_ADDR_CAP1188, &reg_addr, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_master_read_slave(I2C_MASTER_NUM, ESP_ADDR_CAP1188, data, 1);
    return ret;
}

void cap1188_init(void){
    uint8_t data = 0x00;
    cap1188_write(0x27, &data); //모든 터치센서 활성화
    data = 0x3F;
    cap1188_write(0x21, &data); //모든 터치센서 감도 최대
}