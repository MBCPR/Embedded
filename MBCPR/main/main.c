#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

#include "wifi.h"
#include "esp_err.h"
#include "wifi_nvs_manager.h"

#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "ESP_MAIN_LOGIC";

void app_main(void)
{
    esp_err_t req;
    char ssid[32];
    char password[32];
    bool connected = false;

    req = wifi_nvs_manager_init();
    if(req != ESP_OK) {
        ESP_LOGE(TAG, "치명적인 오류 발생! (에러: %s)", esp_err_to_name(req));
        ESP_LOGI(TAG, "5초 후 시스템 재시작...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    
    wifi_init();

    wifi_nvs_reset_search_order();
    while(wifi_nvs_get_next_config(ssid, password) == ESP_OK) {
        ESP_LOGI(TAG, "NVS에 저장된 Wi-Fi (%s) 연결 시도...", ssid);
        if(wifi_connect_sta(ssid, password) == ESP_OK) {
            ESP_LOGI(TAG, "Wi-Fi (%s)에 성공적으로 연결되었습니다.", ssid);
            wifi_nvs_save_config(ssid, password);
            connected = true;
            break;
        } else {
            ESP_LOGW(TAG, "Wi-Fi (%s) 연결 실패. 다음 설정을 시도합니다.", ssid);
        }
    }
    
    while(!connected) {
        ESP_LOGI(TAG, "저장된 Wi-Fi 설정으로 연결할 수 없습니다. SoftAP 모드를 시작합니다.");
        if(softap_get_config(ssid, password) == ESP_OK) {
            ESP_LOGI(TAG, "SoftAP를 통해 Wi-Fi 설정을 받았습니다. 연결 시도...");
            if(wifi_connect_sta(ssid, password) == ESP_OK) {
                ESP_LOGI(TAG, "SoftAP를 통해 받은 Wi-Fi (%s)에 성공적으로 연결되었습니다.", ssid);
                wifi_nvs_save_config(ssid, password);
                connected = true;
            } else {
                ESP_LOGE(TAG, "SoftAP를 통해 받은 Wi-Fi (%s) 연결 실패.", ssid);
            }
        }
    }

    
    esp_ip4_addr_t ip;
    ESP_ERROR_CHECK(wifi_get_ip_addr(&ip));
    ESP_LOGI(TAG, "시스템이 네트워크에 연결되었습니다. IP: " IPSTR, IP2STR(&ip));
}