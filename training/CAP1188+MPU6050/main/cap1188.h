#ifdef CAP1188_H
#define CAP1188_H

#include<stdint.h>
#include  "esp_err.h"

#define ESP_ADDR_CAP1188 0x29

esp_err_t cap1188_write(uint8_t reg_addr, uint8_t *data);
esp_err_t cap1188_read(uint8_t reg_addr, uint8_t *data);
void cap1188_init(void);

#endif