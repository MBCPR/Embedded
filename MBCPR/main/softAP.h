#ifndef SOFTAP_H_
#define SOFTAP_H_

#include "esp_err.h"

esp_err_t softap_get_config(char* ssid, char* password);

#endif /* SOFTAP_H_ */
