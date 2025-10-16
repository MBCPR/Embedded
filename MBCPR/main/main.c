#include "wifi_nvs_manager.h"
#include "wifi.h"
#include "HX711.h"
//#include "MPU6050.h"
//#include "cap1188.h"
//#include "i2c_master.h"
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

#define NVS_NO_DATA_DELAY_MS 10000 // nvs에 데이터가 없을 경우 대기시간
#define CONN_FAIL_DELAY_MS 50 // 와이파이 입력이 없을 시 대기시간

// --- 공용 Wifi ssid, password --- 
static char main_ssid[MAX_SSID_LEN];
static char main_password[MAX_PASSWORD_LEN];

// --- 태스크 관리를 위한 뮤텍스 ---
static SemaphoreHandle_t s_wifi_config_mutex = NULL; // 위의 공용 와이파이 데이터 접근을 관리하는 뮤텍스
SemaphoreHandle_t s_hx711_mutex = NULL;              // 무게센서 값과 타임스탬프 업데이트 접근을 관리하는 뮤텍스 / 다른 파일에서도 사용한다

// --- 종료용 태스크 핸들 ---
TaskHandle_t ap_task_handle = NULL;       // ap 모드 태스크 파괴용 핸들
TaskHandle_t nvs_task_handle = NULL;      // nvs 저장소 접근 태스크 파괴용 
TaskHandle_t app_main_task_handle = NULL;


// ap Wifi 데이터 획득 태스크
void wifi_get_ap_task(void *pvParameters) {
    char ssid_temp[MAX_SSID_LEN];         //
    char password_temp[MAX_PASSWORD_LEN]; // Wifi 데이터 임시 저장 공간

    start_get_wifi(); // AP 모드 시작
    ap_task_handle = xTaskGetCurrentTaskHandle(); // 핸들 저장

    while(1){
		
		// Wifi 값 획득에 성공했을 경우
		if(check_get_wifi(ssid_temp, password_temp) == ESP_OK){
            
            // Mutex 획득 시도
            if (xSemaphoreTake(s_wifi_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                
                // --- 임계 영역 시작 ---
                strncpy(main_ssid, ssid_temp, MAX_SSID_LEN - 1);             // Wifi ssid 데이터 쓰기
                main_ssid[MAX_SSID_LEN - 1] = '\0';
                strncpy(main_password, password_temp, MAX_PASSWORD_LEN - 1); // Wifi password 데이터 쓰기
                main_password[MAX_PASSWORD_LEN - 1] = '\0';
                // --- 임계 영역 종료 ---

                xSemaphoreGive(s_wifi_config_mutex); // 뮤택스 반환
                ESP_LOGI("AP_TASK", "AP 설정 획득 및 main_ssid/password 업데이트 완료.");
            }
		}
		vTaskDelay(pdMS_TO_TICKS(50)); // 짧은 간격으로 Wifi 값 체크
	}
}

// nvs Wifi 데이터 획득 태스크
void wifi_get_nvs_task(void *pvParameters) {
    char ssid_temp[MAX_SSID_LEN];         //
    char password_temp[MAX_PASSWORD_LEN]; // Wifi 데이터 임시 저장 공간
    
    esp_err_t err; // 상태 코드 확인을 위해 err 변수 사용

    nvs_task_handle = xTaskGetCurrentTaskHandle(); // 핸들 저장

    while (1) {
        wifi_nvs_reset_search_order(); // nvs WIfi 검색 순서 초기화

        while (1) {
            err = wifi_nvs_get_next_config(ssid_temp, password_temp); // nvs에서 Wifi 데이터 가져오기
			
			// nvs 에서 성공적으로 Wifi 데이터 가져오기
            if (err == ESP_OK) {
				
                // Mutex 획득 시도
                if (xSemaphoreTake(s_wifi_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    
                    // --- 임계 영역 시작 ---
                    strncpy(main_ssid, ssid_temp, MAX_SSID_LEN - 1);             // Wifi ssid 데이터 쓰기
                    main_ssid[MAX_SSID_LEN - 1] = '\0';
                    strncpy(main_password, password_temp, MAX_PASSWORD_LEN - 1); // Wifi password 데이터 쓰기
                    main_password[MAX_PASSWORD_LEN - 1] = '\0';
                    // --- 임계 영역 종료 ---

                    xSemaphoreGive(s_wifi_config_mutex); // 뮤택스 반환
                    ESP_LOGI("NVS_TASK", "NVS 설정 획득: SSID='%s' 업데이트 완료.", main_ssid);
                }

                vTaskDelay(pdMS_TO_TICKS(200)); // 다음 NVS 설정 시도까지 딜레이

            } // nvs에 더이상 Wifi 데이터가 없을 경우
            else if (err == ESP_ERR_NVS_NOT_FOUND) {
				
                // 10초 휴식 후 검색 재시작
                ESP_LOGW("NVS_TASK", "모든 NVS 설정을 순회했습니다. 10초 휴식 후 다시 시작.");
                vTaskDelay(pdMS_TO_TICKS(NVS_NO_DATA_DELAY_MS));
                
                break; // 내부 루프 탈출, 외부 루프에서 검색 재시작

            } // nvs를 읽는 중 알 수 없는 오류가 생겼을 경우
            else {
				
				// 10초 휴식 후 검색 재시작
                ESP_LOGE("NVS_TASK", "NVS 읽기 중 오류 발생. 10초 후 재시도.");
                vTaskDelay(pdMS_TO_TICKS(NVS_NO_DATA_DELAY_MS));
                
                break;// 내부 루프 탈출, 외부 루프에서 검색 재시작
            }
        }
    }
}

// Wifi 데이터 획득 확인 및 연결 시도 태스크
void wifi_setting(){
	char current_ssid[MAX_SSID_LEN];         //
    char current_password[MAX_PASSWORD_LEN]; // Wifi 데이터 임시 저장 공간

    while (1) {
		
        // Mutex 획득 시도
        if (xSemaphoreTake(s_wifi_config_mutex, portMAX_DELAY) == pdTRUE) {
            
            // --- 임계 영역 시작 ---
            strncpy(current_ssid, main_ssid, MAX_SSID_LEN - 1);             // Wifi ssid 데이터 쓰기
            current_ssid[MAX_SSID_LEN - 1] = '\0';
            strncpy(current_password, main_password, MAX_PASSWORD_LEN - 1); // Wifi password 데이터 쓰기
            current_password[MAX_PASSWORD_LEN - 1] = '\0';
            // --- 임계 영역 종료 ---
            
            xSemaphoreGive(s_wifi_config_mutex); // 뮤택스 반환
			
			// Wifi 데이터가 유효할 경우
            if (strlen(current_ssid) > 0) {
				
				vTaskSuspend(ap_task_handle);  // ap 태스크 일시중지
				vTaskSuspend(nvs_task_handle); // nvs 태스크 일시중지
				
                ESP_LOGI("CONTROL", "연결 시도 중: SSID='%s'", current_ssid);
                
                // Wifi 접속에 성공했을 경우
                if (wifi_connect_sta(current_ssid, current_password) == ESP_OK) {
                    
                    ESP_LOGI("CONTROL", "SUCCESS: Wi-Fi 연결 성공! (SSID: %s)", current_ssid);
                    
                    // Wifi 데이터 수집 태스크 완전 중지
                    if (ap_task_handle) {
                        vTaskDelete(ap_task_handle);
                    }
                    if (nvs_task_handle) {
                        vTaskDelete(nvs_task_handle);
                    }
                    
                    stop_get_wifi(); // AP 모드 중지
                    
                    wifi_nvs_save_config(main_ssid, main_password); // Wifi 데이터 nvs에 등록 및 업데이트
                    
                    if (app_main_task_handle != NULL) {
						
                		// app_main 재개
                		xTaskNotifyGive(app_main_task_handle); 
            		}
                    
                    // 컨트롤 태스크 스스로 종료
                    vTaskDelete(NULL);
                    
                } // Wifi 접속에 실패했을 경우
                else {
                    
                    ESP_LOGW("CONTROL", "연결 실패. %dms 후 다음 설정으로 재시도.", CONN_FAIL_DELAY_MS);
                    
                    // Wifi 데이터 초기화
                    main_ssid[0] = '\0';
    				main_password[0] = '\0';
    				vTaskDelay(pdMS_TO_TICKS(CONN_FAIL_DELAY_MS));
                }
                
                vTaskResume(ap_task_handle);  // ap 태스크 재개
                vTaskResume(nvs_task_handle); // nvs 태스크 재개
            } // Wifi 데이터가 유효하지 않을 경우
            else {
                
                ESP_LOGI("CONTROL", "유효한 SSID 없음. AP/NVS 태스크의 데이터 대기 중...");
                vTaskDelay(pdMS_TO_TICKS(CONN_FAIL_DELAY_MS));
            }
        }
    }
}

// 무게센서 태스크
void hx711_task(void* arg) {
	int hx711; // 무게센서 값 임시 변수 생성
    
    const int delay_ms = 100; // 타임 스태프 업데이크 수치 설정
    
    // 전송 명령이 있을 경우
    while(!g_finished) {
		
        hx711 = hx711_read(); // 무게센서 값 저장
        // 무게센서 값 조정
        hx711 += 200000;
    	hx711 *= 0.0001;
    	hx711 /= 2;
    	
    	// 무게센서 뮤택스 요청
    	if (xSemaphoreTake(s_hx711_mutex, portMAX_DELAY) == pdTRUE) {
			
			// --- 임계 영역 시작 ---
 			if (g_timer_running) {
            	g_timestamp_ms += delay_ms; // 타임 스태프 업데이트
        	}
            g_HX711Value = hx711; // 전역 변수에 저장
            // --- 임계 영역 종료 ---
            
            
            xSemaphoreGive(s_hx711_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
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
    
    esp_err_t req; // 에러 확인용 변수
    
    app_main_task_handle = xTaskGetCurrentTaskHandle(); // 와이파이 연결 작업 종료 메세지를 받기 위한 핸들
    
    //i2c_master_init(); // I2c 초기화

    req = wifi_nvs_manager_init(); // nvs(Wifi) 저장소 초기화
    if(req != ESP_OK) {
        ESP_LOGE(TAG, "치명적인 오류 발생! (에러: %s)", esp_err_to_name(req));
        ESP_LOGI(TAG, "5초 후 시스템 재시작...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    } // 에러 처리
    
    /*--와이파이 설정--*/
    wifi_init(); // Wifi 스택 초기화
    
    s_wifi_config_mutex = xSemaphoreCreateMutex(); // 뮤텍스 생성
	if (s_wifi_config_mutex == NULL) {
	    ESP_LOGE(TAG, "Mutex 생성 실패! 치명적 오류.");
    	vTaskDelay(pdMS_TO_TICKS(5000));
    	esp_restart();
	} // 에러 처리

	wifi_mode_set_apsta(); // apsta 모드 설정
    
    xTaskCreate(wifi_setting, "wifi_setting", 4096, NULL, 6, NULL);                       // 와이파이 입력 관리 태스크 
    xTaskCreate(wifi_get_ap_task, "wifi_get_ap_task", 4096, NULL, 5, &ap_task_handle);    // ap모드 와이파이 입력 태스크
    xTaskCreate(wifi_get_nvs_task, "wifi_get_nvs_task", 4096, NULL, 4, &nvs_task_handle); // nvs 저장소 와이파이 불러오기 태스크
    
    ESP_LOGI(TAG, "Wi-Fi 연결 대기 중...");
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // Wifi 연결이 될 때까지 로직 정지
    
    ESP_LOGI(TAG, "Wi-Fi 연결에 성공했습니다. STA 모드로 전환합니다.");
    
    wifi_mode_crush_ap(); // ap 모드 파괴
    
    esp_ip4_addr_t ip; 
    ESP_ERROR_CHECK(wifi_get_ip_addr(&ip));
    ESP_LOGI(TAG, "시스템이 네트워크에 연결되었습니다. IP: " IPSTR, IP2STR(&ip)); // ip 주소 받아오기
    
    /*--센서 및 웹소켓 초기화 및 태스크--*/
    
    s_hx711_mutex = xSemaphoreCreateMutex(); // 무게센서 값 뮤택스 생성
    if (s_hx711_mutex == NULL) {
    	ESP_LOGE(TAG, "HX711 Mutex 생성 실패! 재시작.");
    	vTaskDelay(pdMS_TO_TICKS(5000));
    	esp_restart();
	}// 에러 처리
    
    hx711_init(); // 무게센서 설정 초기화
    // mpu6050_init(); // MPU6050 초기화 주석 처리
    // cap1188_init(); // CAP1188 초기화 주석 처리
    
    g_last_connect_time = esp_log_timestamp();

    xTaskCreate(hx711_task, "hx711_task", 4096, NULL, 5, NULL); // 무게센서 값 읽기 태스크
    // xTaskCreate(mpu6050_task, "mpu6050_task", 4096, NULL, 5, NULL); // MPU6050 태스크 주석 처리
    
    // 웹소켓 통신 태스크 활성화
    xTaskCreate(start_websocket_client, "websocket_client", 8192, NULL, 5, NULL);   // 웹소켓 활성화 태스크
    xTaskCreate(check_server_message, "check_server_message", 2048, NULL, 5, NULL); // 웹소켓 수신 감지 태스크
}

