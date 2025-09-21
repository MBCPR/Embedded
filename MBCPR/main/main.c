#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

#include "wifi_nvs_manager.h"
#include "softAP.h"

#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


void app_main(void)
{
	esp_err_t req;
	req = wifi_nvs_manager_init();// nvs 초기화
	if(req != ESP_OK){//에러 발생 시
        ESP_LOGE("치명적인 오류 발생! (에러: %s)", esp_err_to_name(error));
        ESP_LOGI("5초 후 시스템 재시작...");
        
        vTaskDelay(pdMS_TO_TICKS(5000)); 

        // 시스템 재시작
        esp_restart();
	}
	
	char ssid[32];
	char password[32];
	
	while(1){
		if(wifi_nvs_get_next_config(ssid, password) == ESP_OK){
			//와이파이 연결 시도 로직
			//연결 성공이라면
			//break;
		}
		else{
			softap_get_config(ssid, password);
			//와이파이 연결 시도 로직
			//연결 성공이라면
			//break;
		}
	}
	
	wifi_nvs_reset_search_order();
	
	
	
}
