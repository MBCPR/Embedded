#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h" 
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "websocket_client.h"

#define WEB_SERVER_ADDR           "ws://13.209.6.11:8080/board?serial=BOARD123" 
#define CHECK_CONNECT_TIME         10000 // 서버 연결 확인 시간 간격 (10초)

// ESP-IDF에서 TickType_t를 ms로 변환하는 매크로 재정의
#define pdMS_TO_TICKS(xTimeInMs) ( ( TickType_t ) ( ( ( TickType_t ) ( xTimeInMs ) * ( TickType_t ) configTICK_RATE_HZ ) / ( TickType_t ) 1000U ) )

const char *WS = "ws_client";
const char *g_WS = "ws_client";

// --- 전역 변수 정의 및 초기화 --- 
extern SemaphoreHandle_t s_hx711_mutex; // hx711.c에서 사용할 뮤텍스 (main.c에서 생성)
bool g_finished = true; // 데이터 전송 종료 상태 (true: 중지됨)
esp_websocket_client_handle_t g_client = NULL;
uint8_t g_CapValue = 0;   // 캡센서 값 (현재 미사용)
int16_t g_mpuValueX = 0;  // MPU6050 X축 값 (현재 미사용)
int16_t g_mpuValueY = 0;  // MPU6050 Y축 값 (현재 미사용)
int16_t g_mpuValueZ = 0;  // MPU6050 Z축 값 (현재 미사용)
int64_t g_last_connect_time = 0; // 마지막 서버 통신 타임스탬프
int g_HX711Value = 0;            // HX711 (무게 센서) 값
int64_t g_timestamp_ms = 0;      // 데이터 누적 타임스탬프 (ms)
bool g_timer_running = false;    // 타임스탬프 업데이트 여부 (true: 실행 중) 

// 웹소켓 이벤트 핸들러
void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch(event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(WS, "WEBSOCKET 연결 성공");
            
            g_last_connect_time = esp_log_timestamp(); // 연결 시간 기록
            g_finished = false;                        // 전송 준비 완료 (START 명령 대기)
            g_timer_running = true;                    // 타이머 시작 (CONNECTED 시점부터)
            break;
            
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(WS, "WEBSOCKET 연결 끊김");
            // 연결 끊김 시 5초 후 재연결 시도
            if (g_client != NULL) {
                esp_websocket_client_start(g_client);
                vTaskDelay(pdMS_TO_TICKS(5000));
            }
            break;
            
        case WEBSOCKET_EVENT_DATA:
            ESP_LOGI(WS, "Received=%.*s\n", data->data_len, (char*)data->data_ptr);

            // --- 서버 명령어 처리 ---
            if(data->data_len == 5 && strncmp((char*)data->data_ptr, "CHECK", 5) == 0) {
                g_last_connect_time = esp_log_timestamp(); // 통신 시간 업데이트

                const char *check_msg = "YES";
                esp_websocket_client_send_text((esp_websocket_client_handle_t)handler_args, check_msg, strlen(check_msg), portMAX_DELAY);
                ESP_LOGI(WS, "서버와 연결 확인");
            }

            if(data->data_len == 5 && strncmp((char*)data->data_ptr, "START", 5) == 0) { 
                g_last_connect_time = esp_log_timestamp(); // 통신 시간 업데이트
                g_timestamp_ms = 0;     // 타임스탬프 초기화
        		g_timer_running = true; // 타이머 실행
                g_finished = false;     // 데이터 전송 허용

                const char *start_msg = "ACCEPTED";
                esp_websocket_client_send_text((esp_websocket_client_handle_t)handler_args, start_msg, strlen(start_msg), portMAX_DELAY);
                ESP_LOGI(WS, "데이터 전송 시작");
            }
            if(data->data_len == 4 && strncmp((char*)data->data_ptr, "STOP", 4) == 0) { 
                g_timer_running = false; // 타이머 정지
                g_finished = true;       // 데이터 전송 중지

                const char *stop_msg = "STOPPED";
                esp_websocket_client_send_text((esp_websocket_client_handle_t)handler_args, stop_msg, strlen(stop_msg), portMAX_DELAY);
                ESP_LOGI(WS, "데이터 전송 종료");
            }

            if(data->data_len == 4 && strncmp((char*)data->data_ptr, "PING", 4) == 0) {
                g_last_connect_time = esp_log_timestamp(); // 통신 시간 업데이트

                const char *pong_msg = "PONG";
                esp_websocket_client_send_text((esp_websocket_client_handle_t)handler_args, pong_msg, strlen(pong_msg), portMAX_DELAY);
                ESP_LOGI(WS, "PONG 응답");
            }
            break;
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGI(WS, "WEBSOCKET 에러");
            break;
        default:
            break;
    }
}

// 웹소켓 통신 시작 태스크
void start_websocket_client(void *arg) {
    esp_websocket_client_config_t websocket_cfg = {
        .uri = WEB_SERVER_ADDR,
    };

    g_client = esp_websocket_client_init(&websocket_cfg); 
    esp_websocket_register_events(g_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)g_client); 
    esp_websocket_client_start(g_client); // 웹소켓 클라이언트 시작

    while(1) { 
		
        if(esp_websocket_client_is_connected(g_client)) {
			 
            if(!g_finished) { // START 명령을 받아 데이터 전송이 허용된 상태
				
                char message[100];
                double hx711;
                int64_t timestamp_safe = 0;
                
                // --- HX711 뮤텍스 획득 시도 ---
                if (xSemaphoreTake(s_hx711_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            		hx711 = (double)g_HX711Value; // 전역 변수 값 복사
            		timestamp_safe = g_timestamp_ms;
            		xSemaphoreGive(s_hx711_mutex); // 뮤텍스 반환
            
            		// JSON 형식으로 메시지 구성
            		snprintf(message, sizeof(message), "{\"pressure\": %lf, \"timestamp\": %lld}", hx711, timestamp_safe); 
            	
            		// 웹소켓을 통해 데이터 전송
            		esp_websocket_client_send_text(g_client, message, strlen(message), portMAX_DELAY);
            		ESP_LOGW(WS, "hx711: %lf/timestamp: %lld/파싱했습니다.", hx711, timestamp_safe); 
        		}
        		else {
					
        		    ESP_LOGW(WS, "뮤텍스 획득 실패, HX711 데이터 전송 건너뜀.");
        		}
            }
            else {
				
                 ESP_LOGI(WS, "데이터 전송 중지 상태. 연결은 유지됨.");
            }
	        vTaskDelay(pdMS_TO_TICKS(100)); // 100ms 주기로 데이터 전송 시도
	        
        }
        else {
			
            ESP_LOGW(WS, "웹소켓 서버 연결 대기중...");
            
            esp_websocket_client_start(g_client); // 연결 시도
            
            vTaskDelay(pdMS_TO_TICKS(5000)); // 5초 대기 후 재시도
        }
    }
}

// 서버와의 연결 상태 확인 및 타임아웃 처리 태스크
void check_server_message(void *arg) {
    while(1) { 
        int64_t current_time = esp_log_timestamp(); 
        // 연결된 상태에서 마지막 통신 이후 CHECK_CONNECT_TIME(10초)이 경과했다면
        if(esp_websocket_client_is_connected(g_client) && (current_time - g_last_connect_time > CHECK_CONNECT_TIME)) {
            ESP_LOGW(WS, "서버와의 연결 끊김. 재연결 시도중...");
            
            if(g_client != NULL) {
                esp_websocket_client_stop(g_client); // 클라이언트를 멈춰서 재연결을 유도
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}