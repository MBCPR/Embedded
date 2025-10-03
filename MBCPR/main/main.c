#include "wifi_nvs_manager.h"
#include "wifi.h"
#include "HX711.h"
#include "MPU6050.h" // 초기화 및 태스크는 주석 처리
#include "cap1188.h" // 초기화 및 태스크는 주석 처리
#include "i2c_master.h"
#include "websocket_client.h"

#include "esp_err.h"
#include "esp_websocket_client.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "ESP_MAIN_LOGIC";
#define SOFTAP_TIMEOUT_MINUTES  5 // SoftAP 대기 시간 제한 (5분)

// 미리 저장된 와이파이 데이터를 가져와 연결 시도
esp_err_t wifi_conect_nvs() {
    char ssid[32];
    char password[32];
    
    wifi_nvs_reset_search_order();
    
    while(wifi_nvs_get_next_config(ssid, password) == ESP_OK) {
        
        ESP_LOGI(TAG, "NVS에 저장된 Wi-Fi (%s) 연결 시도...", ssid);
        
        if(wifi_connect_sta(ssid, password) == ESP_OK) {
            ESP_LOGI(TAG, "Wi-Fi (%s)에 성공적으로 연결되었습니다.", ssid);
            wifi_nvs_save_config(ssid, password);
            return ESP_OK;
        } else {
            ESP_LOGW(TAG, "Wi-Fi (%s) 연결 실패. 다음 설정을 시도합니다.", ssid);
        }
    }
    
    return ESP_FAIL;
}

// 무게센서 태스크
void hx711_task(void* arg) {
    
    while(!g_finished) {
        g_HX711Value = hx711_read();
        // 값 가공 부분
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    // finished = true 일 때 데이터 수집 중단됨 (태스크 종료)
    vTaskDelete(NULL); 
}

/*
// 기울기 센서 태스크 (주석 처리됨)
void mpu6050_task(void *arg) {
    
    while(!g_finished) {
        if(mpu6050_read_accel(&g_mpuValueX, &g_mpuValueY, &g_mpuValueZ) == ESP_OK) {
            ESP_LOGD(TAG, "read: X=%d, Y=%d, Z=%d", g_mpuValueX, g_mpuValueY, g_mpuValueZ);
        } else {
            ESP_LOGE(TAG, "MPU6050 read error");
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
    // finished = true 일 때 데이터 수집 중단됨 (태스크 종료)
    vTaskDelete(NULL);
}
*/

void app_main(void) {
    /*--시스템 초기화--*/
    
    esp_err_t req;
    
    // I2C 초기화는 여기서 단 한 번만 수행
    i2c_master_init(); 

    req = wifi_nvs_manager_init();
    if(req != ESP_OK) {
        ESP_LOGE(TAG, "치명적인 오류 발생! (에러: %s)", esp_err_to_name(req));
        ESP_LOGI(TAG, "5초 후 시스템 재시작...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    
    /*--와이파이 스택 초기화--*/
    wifi_init();
    
    bool is_connected = false;
    
    // 1. NVS에 저장된 설정으로 연결 시도
    if (wifi_conect_nvs() == ESP_OK) {
        is_connected = true;
    }
    
    // 2. 저장된 설정으로 연결 실패 시 SoftAP 모드 시작 (복구 루프)
    if (!is_connected) {
        ESP_LOGW(TAG, "저장된 설정으로 연결 실패. SoftAP 대기 모드로 전환합니다.");
        
        int softap_timeout_seconds = SOFTAP_TIMEOUT_MINUTES * 60;
        int elapsed_time = 0; 
        char new_ssid[32];
        char new_password[32];
        
        while (!is_connected && elapsed_time < softap_timeout_seconds) {
            
            // SoftAP를 열고 HTTP 서버 시작 (APSTA 모드)
            softap_get_wifi(new_ssid, new_password);

            // NVS AP 감지 및 자동 연결 시도 (SoftAP + STA 모드이므로 가능)
            if (wifi_scan_nvs_check() == ESP_OK) {
                is_connected = true;
                ESP_LOGI(TAG, "NVS Wi-Fi 감지로 SoftAP 중단 및 STA 모드 전환 성공.");
                break;
            }
            
            // SoftAP를 통한 설정 수신 확인
            if (g_check_ssid && g_check_password) {
                // SoftAP를 통해 설정 수신. 연결 시도.
                if(wifi_connect_sta(new_ssid, new_password) == ESP_OK) {
                    ESP_LOGI(TAG, "SoftAP를 통해 받은 Wi-Fi (%s)에 성공적으로 연결되었습니다.", new_ssid);
                    wifi_nvs_save_config(new_ssid, new_password);
                    is_connected = true;
                    break;
                } else {
                    ESP_LOGE(TAG, "SoftAP를 통해 받은 Wi-Fi 연결 실패. 재시도합니다.");
                }
                // check 플래그 초기화
                g_check_ssid = false;
                g_check_password = false;
            }
            
            vTaskDelay(pdMS_TO_TICKS(1000));
            elapsed_time++;
        }

        // SoftAP 시간 제한 초과 시 시스템 재시작
        if (!is_connected && elapsed_time >= softap_timeout_seconds) {
            ESP_LOGW(TAG, "SoftAP 대기 시간(%d분) 초과. 시스템을 재시작합니다.", SOFTAP_TIMEOUT_MINUTES);
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_restart();
        }
    }
    
    
    /*--센서 및 웹소켓 초기화 및 태스크--*/
    if (is_connected) {
        esp_ip4_addr_t ip;
        ESP_ERROR_CHECK(wifi_get_ip_addr(&ip));
        ESP_LOGI(TAG, "시스템이 네트워크에 연결되었습니다. IP: " IPSTR, IP2STR(&ip));
        
        hx711_init();
        // mpu6050_init(); // MPU6050 초기화 주석 처리
        // cap1188_init(); // CAP1188 초기화 주석 처리
        g_last_connect_time = esp_log_timestamp();
        g_finished = false; // 연결 성공 시 데이터 전송 시작

        // 무게센서 태스크 활성화
        xTaskCreate(hx711_task, "hx711_task", 4096, NULL, 5, NULL);
        // xTaskCreate(mpu6050_task, "mpu6050_task", 4096, NULL, 5, NULL); // MPU6050 태스크 주석 처리
        
        // 웹소켓 통신 태스크 활성화
        xTaskCreate(start_websocket_client, "websocket_client", 8192, NULL, 5, NULL);
        xTaskCreate(check_server_message, "check_server_message", 2048, NULL, 5, NULL);
    }
}
