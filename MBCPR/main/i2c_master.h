#ifndef I2C_MASTER_H_
#define I2C_MASTER_H_

#include "esp_err.h"
#include "driver/i2c.h"

#define pdMS_TO_TICKS(xTimeInMs) ( ( TickType_t ) ( ( ( TickType_t ) ( xTimeInMs ) * ( TickType_t ) configTICK_RATE_HZ ) / ( TickType_t ) 1000U ) )

#define I2C_MASTER_SCL_IO           22          
#define I2C_MASTER_SDA_IO           21          
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          100000     
#define I2C_MASTER_TX_BUF_DISABLE   0           
#define I2C_MASTER_RX_BUF_DISABLE   0

esp_err_t i2c_master_write_slave(i2c_port_t i2c_num, uint8_t slave_addr, uint8_t *data_wr, size_t size);
esp_err_t i2c_master_read_slave(i2c_port_t i2c_num, uint8_t slave_addr, uint8_t *data_rd, size_t size);
esp_err_t i2c_master_read_reg_slave(i2c_port_t i2c_num, uint8_t slave_addr, uint8_t reg_addr, uint8_t *data_rd, size_t size);
void i2c_master_init(void);

#endif /* I2C_MASTER_H_ */