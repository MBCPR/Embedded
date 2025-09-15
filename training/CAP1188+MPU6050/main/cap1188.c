#include "cap1188.h"
#include "driver/i2c.h"

#define ESP_ADDR_CAP1188 0x29

#define pdMS_TO_TICKS(xTimeInMs) ( ( TickType_t ) ( ( ( TickType_t ) ( xTimeInMs ) * ( TickType_t ) configTICK_RATE_HZ ) / ( TickType_t ) 1000U ) )

#define I2C_MASTER_SCL_IO           22          
#define I2C_MASTER_SDA_IO           21          
#define I2C_MASTER_NUM              I2C_NUM_0   
#define I2C_MASTER_FREQ_HZ          100000     
#define I2C_MASTER_TX_BUF_DISABLE   0           
#define I2C_MASTER_RX_BUF_DISABLE   0

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
esp_err_t i2c_master_read_slave(i2c_port_t i2c_num, uint8_t slave_addr, uint8_t *data_rd, size_t size)
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
esp_err_t i2c_master_write_slave(i2c_port_t i2c_num, uint8_t slave_addr, uint8_t *data_wr, size_t size)
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