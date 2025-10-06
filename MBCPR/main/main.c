#include "wifi_nvs_manager.h"
#include "wifi.h"
#include "HX711.h"
#include "MPU6050.h" // 초기화 및 태스크는 주석 처리
#include "cap1188.h" // 초기화 및 태스크는 주석 처리
#include "i2c_master.h"
#include "websocket_client.h"

#include <string.h>
#include "esp_err.h"
#include "esp_websocket_client.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "ESP_MAIN_LOGIC";
#define SOFTAP_TIMEOUT_MINUTES  5 // SoftAP 대기 시간 제한 (5분)

#define NVS_NO_DATA_DELAY_MS 10000 
#define CONN_FAIL_DELAY_MS 50

static char main_ssid[MAX_SSID_LEN];
static char main_password[MAX_PASSWORD_LEN];
static SemaphoreHandle_t s_wifi_config_mutex = NULL; // 공유 변수 보호 Mutex

// --- 태스크 핸들 (종료를 위해) ---
TaskHandle_t ap_task_handle = NULL;
TaskHandle_t nvs_task_handle = NULL;
TaskHandle_t app_main_task_handle = NULL;

void wifi_get_ap_task(void *pvParameters) {
    char ssid_temp[MAX_SSID_LEN];
    char password_temp[MAX_PASSWORD_LEN];

    start_get_wifi(); // AP 모드 데이터 수집 시작
    ap_task_handle = xTaskGetCurrentTaskHandle(); // 핸들 저장

    while(1){
		if(check_get_wifi(ssid_temp, password_temp) == ESP_OK){
            
            // Mutex 획득 시도: 100틱 대기
            if (xSemaphoreTake(s_wifi_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                
                // --- 임계 영역 (Critical Section) 시작 ---
                strncpy(main_ssid, ssid_temp, MAX_SSID_LEN - 1);
                main_ssid[MAX_SSID_LEN - 1] = '\0';
                strncpy(main_password, password_temp, MAX_PASSWORD_LEN - 1);
                main_password[MAX_PASSWORD_LEN - 1] = '\0';
                // --- 임계 영역 종료 ---

                xSemaphoreGive(s_wifi_config_mutex);
                ESP_LOGI("AP_TASK", "AP 설정 획득 및 main_ssid/password 업데이트 완료.");
            }
		}
		vTaskDelay(pdMS_TO_TICKS(50)); // 짧은 간격으로 AP 모드 체크
	}
    vTaskDelete(NULL); 
}

void wifi_get_nvs_task(void *pvParameters) {
    char ssid_temp[MAX_SSID_LEN];
    char password_temp[MAX_PASSWORD_LEN];
    esp_err_t err;

    nvs_task_handle = xTaskGetCurrentTaskHandle(); // 핸들 저장

    while (1) {
        wifi_nvs_reset_search_order(); // 검색 순서 초기화

        while (1) {
            err = wifi_nvs_get_next_config(ssid_temp, password_temp);

            if (err == ESP_OK) {
                // Mutex 획득 시도
                if (xSemaphoreTake(s_wifi_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    
                    // --- 임계 영역 시작 ---
                    strncpy(main_ssid, ssid_temp, MAX_SSID_LEN - 1);
                    main_ssid[MAX_SSID_LEN - 1] = '\0';
                    strncpy(main_password, password_temp, MAX_PASSWORD_LEN - 1);
                    main_password[MAX_PASSWORD_LEN - 1] = '\0';
                    // --- 임계 영역 종료 ---

                    xSemaphoreGive(s_wifi_config_mutex);
                    ESP_LOGI("NVS_TASK", "NVS 설정 획득: SSID='%s' 업데이트 완료.", main_ssid);
                }

                vTaskDelay(pdMS_TO_TICKS(200)); // 다음 NVS 설정 시도까지 1초 딜레이

            } else if (err == ESP_ERR_NVS_NOT_FOUND) {
                // 요청하신 로직: 데이터 없음 -> 10초 휴식 후 검색 재시작
                ESP_LOGW("NVS_TASK", "모든 NVS 설정을 순회했습니다. 10초 휴식 후 다시 시작.");
                vTaskDelay(pdMS_TO_TICKS(NVS_NO_DATA_DELAY_MS));
                break; // 내부 루프 탈출, 외부 루프에서 검색 재시작

            } else {
                ESP_LOGE("NVS_TASK", "NVS 읽기 중 오류 발생. 10초 후 재시도.");
                vTaskDelay(pdMS_TO_TICKS(NVS_NO_DATA_DELAY_MS));
                break; 
            }
        }
    }
    vTaskDelete(NULL);
}

void wifi_setting(){
	char current_ssid[MAX_SSID_LEN];
    char current_password[MAX_PASSWORD_LEN];

    while (1) {
        // 1. Mutex를 획득하여 현재 main_ssid/password를 복사합니다.
        if (xSemaphoreTake(s_wifi_config_mutex, portMAX_DELAY) == pdTRUE) {
            
            // 공유 변수 복사 (읽기 작업)
            strncpy(current_ssid, main_ssid, MAX_SSID_LEN - 1);
            current_ssid[MAX_SSID_LEN - 1] = '\0';
            strncpy(current_password, main_password, MAX_PASSWORD_LEN - 1);
            current_password[MAX_PASSWORD_LEN - 1] = '\0';
            
            xSemaphoreGive(s_wifi_config_mutex);

            // 2. 유효한 설정이 있을 경우 연결을 시도합니다.
            if (strlen(current_ssid) > 0) {
                ESP_LOGI("CONTROL", "연결 시도 중: SSID='%s'", current_ssid);
                
                if (wifi_connect_sta(current_ssid, current_password) == ESP_OK) {
                    
                    // 3. 연결 성공! 요청하신 로직에 따라 태스크 모두 삭제
                    ESP_LOGI("CONTROL", "SUCCESS: Wi-Fi 연결 성공! (SSID: %s)", current_ssid);
                    
                    // 두 설정 수집 태스크를 삭제합니다.
                    if (ap_task_handle) {
                        vTaskDelete(ap_task_handle);
                    }
                    if (nvs_task_handle) {
                        vTaskDelete(nvs_task_handle);
                    }
                    
                    stop_get_wifi(); // AP 모드 정리
                    
                    wifi_nvs_save_config(main_ssid, main_password);
                    
                    if (app_main_task_handle != NULL) {
                		// app_main 태스크를 대기 상태에서 깨우기 위해 알림을 보냅니다.
                		xTaskNotifyGive(app_main_task_handle); 
            		}
                    
                    // 컨트롤 태스크 자신도 삭제하고 종료합니다.
                    vTaskDelete(NULL);
                    
                }
                else {
                    // 4. 연결 실패. 다음 설정이 업데이트되기를 기다리며 잠시 대기
                    ESP_LOGW("CONTROL", "연결 실패. %dms 후 다음 설정으로 재시도.", CONN_FAIL_DELAY_MS);
                    main_ssid[0] = '\0';
    				main_password[0] = '\0';
    				vTaskDelay(pdMS_TO_TICKS(CONN_FAIL_DELAY_MS));
                }
            }
            else {
                 // main_ssid가 비어있을 경우, 데이터 수집을 기다립니다.
                ESP_LOGI("CONTROL", "유효한 SSID 없음. AP/NVS 태스크의 데이터 대기 중...");
                vTaskDelay(pdMS_TO_TICKS(CONN_FAIL_DELAY_MS));
            }
        }
    }
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
    
    app_main_task_handle = xTaskGetCurrentTaskHandle();
    
    // I2C 초기화는 여기서 단 한 번만 수행
    //i2c_master_init(); 

    req = wifi_nvs_manager_init();
    if(req != ESP_OK) {
        ESP_LOGE(TAG, "치명적인 오류 발생! (에러: %s)", esp_err_to_name(req));
        ESP_LOGI(TAG, "5초 후 시스템 재시작...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    
    /*--와이파이 설정--*/
    wifi_init();
    
    s_wifi_config_mutex = xSemaphoreCreateMutex(); 
	if (s_wifi_config_mutex == NULL) {
	    ESP_LOGE(TAG, "Mutex 생성 실패! 치명적 오류.");
    	vTaskDelay(pdMS_TO_TICKS(5000));
    	esp_restart();
	}

	wifi_mode_set_apsta();
    
    xTaskCreate(wifi_get_ap_task, "wifi_get_ap_task", 4096, NULL, 5, &ap_task_handle);
    xTaskCreate(wifi_get_nvs_task, "wifi_get_nvs_task", 4096, NULL, 4, &nvs_task_handle);
    xTaskCreate(wifi_setting, "wifi_setting", 4096, NULL, 6, NULL);
    
    ESP_LOGI(TAG, "Wi-Fi 연결 대기 중...");
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    
    ESP_LOGI(TAG, "Wi-Fi 연결에 성공했습니다. STA 모드로 전환합니다.");
    
    esp_err_t wifi_mode_crush_ap(void);
    
    esp_ip4_addr_t ip;
    ESP_ERROR_CHECK(wifi_get_ip_addr(&ip));
    ESP_LOGI(TAG, "시스템이 네트워크에 연결되었습니다. IP: " IPSTR, IP2STR(&ip));
    
    /*--센서 및 웹소켓 초기화 및 태스크--*/
    
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

