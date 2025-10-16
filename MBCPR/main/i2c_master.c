#include "esp_err.h"
#include "driver/i2c.h"
#include "i2c_master.h"
#include "freertos/FreeRTOS.h"


void i2c_master_init() {
    // --- I2C 마스터 설정 초기화 ---
    int i2c_master_port = I2C_MASTER_NUM;
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;             // 마스터 모드 설정
    conf.sda_io_num = I2C_MASTER_SDA_IO;     // SDA 핀 설정
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE; // SDA 풀업 활성화
    conf.scl_io_num = I2C_MASTER_SCL_IO;     // SCL 핀 설정
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE; // SCL 풀업 활성화
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ; // 클럭 속도 설정
    i2c_param_config(i2c_master_port, &conf);
    i2c_driver_install(i2c_master_port, conf.mode,
                       I2C_MASTER_RX_BUF_DISABLE, // RX 버퍼 비활성화
                       I2C_MASTER_TX_BUF_DISABLE, // TX 버퍼 비활성화
                       0);
}

// 마스터가 슬레이브에서 데이터를 읽는 함수
esp_err_t i2c_master_read_slave(i2c_port_t i2c_num, uint8_t slave_addr, uint8_t *data_rd, size_t size) {
    if (size == 0) { // 읽을 데이터가 없으면 성공 반환
        return ESP_OK;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create(); // 명령어 링크 생성

    i2c_master_start(cmd);
    // 슬레이브 주소 + READ 비트 (ACK 기대)
    i2c_master_write_byte(cmd, (slave_addr << 1) | I2C_MASTER_READ, true); 
    if (size > 1) { // 2바이트 이상일 경우
        // 마지막 1바이트를 제외하고 ACK 응답으로 읽기
        i2c_master_read(cmd, data_rd, size - 1, I2C_MASTER_ACK);
    }
    // 마지막 1바이트를 NACK 응답으로 읽기
    i2c_master_read_byte(cmd, data_rd + size - 1, I2C_MASTER_NACK); 
    i2c_master_stop(cmd); // STOP 조건 전송

    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd); // 명령어 링크 삭제
    return ret;
}

// 레지스터 주소 지정 후 반복 시작(Repeated Start)으로 읽는 함수
esp_err_t i2c_master_read_reg_slave(i2c_port_t i2c_num, uint8_t slave_addr, uint8_t reg_addr, uint8_t *data_rd, size_t size) {
    if (size == 0) {
        return ESP_OK;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    // 1. 레지스터 주소 쓰기 (Write)
    i2c_master_start(cmd);
    // 슬레이브 주소 + WRITE 비트 (ACK 기대)
    i2c_master_write_byte(cmd, (slave_addr << 1) | I2C_MASTER_WRITE, true); 
    i2c_master_write_byte(cmd, reg_addr, true); // 레지스터 주소 쓰기

    // 2. 반복 시작 (Repeated Start)
    i2c_master_start(cmd);
    
    // 3. 데이터 읽기 (Read)
    // 슬레이브 주소 + READ 비트 (ACK 기대)
    i2c_master_write_byte(cmd, (slave_addr << 1) | I2C_MASTER_READ, true);
    if (size > 1) {
        // 마지막 1바이트를 제외하고 ACK 응답으로 읽기
        i2c_master_read(cmd, data_rd, size - 1, I2C_MASTER_ACK);
    }
    // 마지막 1바이트를 NACK 응답으로 읽기
    i2c_master_read_byte(cmd, data_rd + size - 1, I2C_MASTER_NACK);
    
    // 4. Stop
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret;
}


// 마스터가 슬레이브에 데이터를 쓰는 함수
esp_err_t i2c_master_write_slave(i2c_port_t i2c_num, uint8_t slave_addr, uint8_t *data_wr, size_t size) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    // 슬레이브 주소 + WRITE 비트 (ACK 기대)
    i2c_master_write_byte(cmd, (slave_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data_wr, size, true); // 데이터 쓰기 (ACK 기대)
    i2c_master_stop(cmd); // STOP 조건 전송
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret;
}