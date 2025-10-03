#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h" 
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "websocket_client.h"

#define WEB_SERVER_ADDR           "임시 빈칸" 
#define CHECK_CONNECT_TIME         10000

#define pdMS_TO_TICKS(xTimeInMs) ( ( TickType_t ) ( ( ( TickType_t ) ( xTimeInMs ) * ( TickType_t ) configTICK_RATE_HZ ) / ( TickType_t ) 1000U ) )

const char *WS = "ws_client";
const char *g_WS = "ws_client";

// 전역 변수 정의 및 초기화 (사용하지 않는 센서 변수는 초기화 값 유지)
bool g_finished = false;
esp_websocket_client_handle_t g_client = NULL;
uint8_t g_CapValue = 0;
int16_t g_mpuValueX = 0;
int16_t g_mpuValueY = 0;
int16_t g_mpuValueZ = 0;
int64_t g_last_connect_time = 0;
int g_HX711Value = 0;


//웹소켓 이벤트 핸들러
void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch(event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(WS, "WEBSOCKET 연결 성공");
            g_last_connect_time = esp_log_timestamp();
            g_finished = false; 
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(WS, "WEBSOCKET 연결 끊김");
            break;
        case WEBSOCKET_EVENT_DATA:
            ESP_LOGI(WS, "Received=%.*s\n", data->data_len, (char*)data->data_ptr);

            if(data->data_len == 5 && strncmp((char*)data->data_ptr, "CHECK", 5) == 0) {
                g_last_connect_time = esp_log_timestamp();

                const char *check_msg = "YES";
                esp_websocket_client_send_text((esp_websocket_client_handle_t)handler_args, check_msg, strlen(check_msg), portMAX_DELAY);
                ESP_LOGI(WS, "서버와 연결 확인");
            }

            if(data->data_len == 5 && strncmp((char*)data->data_ptr, "START", 5) == 0) { 
                g_last_connect_time = esp_log_timestamp();
                g_finished = false; 

                const char *start_msg = "ACCEPTED";
                esp_websocket_client_send_text((esp_websocket_client_handle_t)handler_args, start_msg, strlen(start_msg), portMAX_DELAY);
                ESP_LOGI(WS, "데이터 전송 시작");
            }
            if(data->data_len == 4 && strncmp((char*)data->data_ptr, "STOP", 4) == 0) { 
                g_finished = true; 

                const char *stop_msg = "STOPPED";
                esp_websocket_client_send_text((esp_websocket_client_handle_t)handler_args, stop_msg, strlen(stop_msg), portMAX_DELAY);
                ESP_LOGI(WS, "데이터 전송 종료");
            }

            if(data->data_len == 4 && strncmp((char*)data->data_ptr, "PING", 4) == 0) {
                g_last_connect_time = esp_log_timestamp();

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

//웹소켓 통신 시작
void start_websocket_client(void *arg) {
    esp_websocket_client_config_t websocket_cfg = {
        .uri = WEB_SERVER_ADDR,
    };

    g_client = esp_websocket_client_init(&websocket_cfg); 
    esp_websocket_register_events(g_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)g_client); 
    esp_websocket_client_start(g_client); 

    while(1) { 
        if(esp_websocket_client_is_connected(g_client)) { 
            if(!g_finished) { 
                char message[100];
                // HX711 데이터만 전송
                snprintf(message, sizeof(message), "{\"HX\": %d}", g_HX711Value); 
                
                esp_websocket_client_send_text(g_client, message, strlen(message), portMAX_DELAY); 
            } else {
                 ESP_LOGI(WS, "데이터 전송 중지 상태. 연결은 유지됨.");
            }
        } else {
            ESP_LOGW(WS, "웹소켓 서버 연결 대기중...");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

//서버와의 연결 확인
void check_server_message(void *arg) {
    while(1) { 
        int64_t current_time = esp_log_timestamp(); 
        if(esp_websocket_client_is_connected(g_client) && (current_time - g_last_connect_time > CHECK_CONNECT_TIME)) {
            ESP_LOGW(WS, "서버와의 연결 끊김. 재연결 시도중...");
            if(g_client != NULL) {
                esp_websocket_client_stop(g_client); 
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}