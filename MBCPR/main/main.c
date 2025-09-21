#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

#include "STA.h"
#include "esp_err.h"
#include "wifi_nvs_manager.h"
#include "softAP.h"

#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

char* TAG = "ESP_MAIN_LOGIC";

void app_main(void)
{
	esp_err_t req;
	req = wifi_nvs_manager_init();// nvs 초기화
	if(req != ESP_OK){//에러 발생 시
        ESP_LOGE(TAG, "치명적인 오류 발생! (에러: %s)", esp_err_to_name(req));
        ESP_LOGI(TAG, "5초 후 시스템 재시작...");
        
        vTaskDelay(pdMS_TO_TICKS(5000)); 

        // 시스템 재시작
        esp_restart();
	}
	
	char ssid[32];
	char password[32];
	
	while(1){
		if(wifi_nvs_get_next_config(ssid, password) == ESP_OK){
			wifi_sta_init();
			if(wifi_connect_sta(ssid, password) == ESP_OK){
				break;
			}
		}
		else{
			softap_get_config(ssid, password);
			wifi_sta_init();
			if(wifi_connect_sta(ssid, password) == ESP_OK){
				break;
			}
		}
	}
	
	wifi_nvs_reset_search_order();
	
	esp_ip4_addr_t ip;
	
	ESP_ERROR_CHECK(wifi_get_ip_addr(&ip));
	
}
