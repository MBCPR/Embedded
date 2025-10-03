#include<stdio.h>
#include<string.h>
#include "freertos/FreeRTOS.h" 
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "websocket_client.h"

#define WEB_SERVER_ADDR           "URI(추가예정)"        
#define CHECK_CONNECT_TIME         10000

#define pdMS_TO_TICKS(xTimeInMs) ( ( TickType_t ) ( ( ( TickType_t ) ( xTimeInMs ) * ( TickType_t ) configTICK_RATE_HZ ) / ( TickType_t ) 1000U ) )


//웹소켓 이벤트 핸들러
void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data){
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch(event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(WS, "WEBSOCKET 연결 성공");
            last_connect_time = esp_log_timestamp();
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(WS, "WEBSOCKET 연결 끊김");
            break;
        case WEBSOCKET_EVENT_DATA:
            ESP_LOGI(WS, "Received=%.*s\n", data->data_len, (char*)data->data_ptr); //서버에서 받은 메시지 출력

            if(data->data_len == 5 && strncmp((char*)data->data_ptr, "CHECK", 5) == 0){ //CHECK 받으면 연결 확인 하는 거
                last_connect_time = esp_log_timestamp();

                const char *check_msg = "YES";
                esp_websocket_client_send_text((esp_websocket_client_handle_t)handler_args, check_msg, strlen(check_msg), portMAX_DELAY);
                ESP_LOGI(WS, "서버와 연결 확인");
            }

            if(data->data_len == 5 && strncmp((char*)data->data_ptr, "START", 5) == 0){ //start 받으면 데이터 전송 시작
                last_connect_time = esp_log_timestamp();
                finished = false;

                const char *start_msg = "ACCEPTED";
                esp_websocket_client_send_text((esp_websocket_client_handle_t)handler_args, start_msg, strlen(start_msg), portMAX_DELAY);
                ESP_LOGI(WS, "데이터 전송 시작");
            }
            if(data->data_len == 4 && strncmp((char*)data->data_ptr, "STOP", 4) == 0){ //stop 받으면 전송 종료
                finished = true;

                const char *stop_msg = "STOPPED";
                esp_websocket_client_send_text((esp_websocket_client_handle_t)handler_args, stop_msg, strlen(stop_msg), portMAX_DELAY);
                ESP_LOGI(WS, "데이터 전송 종료");
            }

            if(data->data_len == 4 && strncmp((char*)data->data_ptr, "PING", 4) == 0){ //ping 받으면 pong 응답하고 시간 기록
                last_connect_time = esp_log_timestamp();

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
void start_websocket_client(void *arg){
    esp_websocket_client_config_t websocket_cfg = {
        .uri = WEB_SERVER_ADDR,
    };

    client = esp_websocket_client_init(&websocket_cfg); //초기화
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client); //이벤트 핸들러 등록
    esp_websocket_client_start(client); //시작

    while(!finished){
        if(esp_websocket_client_is_connected(client)){ //서버 연결 됐을 때
        char message[100];
        snprintf(message, sizeof(message), "{\"HX\": %d, \"mpu_x\": %d, \"mpu_y\": %d, \"mpu_z\": %d}", HX711Value, mpuValueX, mpuValueY, mpuValueZ); //메시지 포맷팅
        esp_websocket_client_send_text(client, message, strlen(message), portMAX_DELAY); //메시지 전송
        }
        else {
            ESP_LOGW(WS, "웹소켓 서버 연결 대기중...");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    esp_websocket_client_stop(client);
    esp_websocket_client_destroy(client); //웹소켓 클라이언트 종료
    vTaskDelete(NULL);
}

//서버와의 연결 확인
void check_server_message(void *arg){
    while(!finished){
        int64_t current_time = esp_log_timestamp(); //현재 시간
        if(esp_websocket_client_is_connected(client) && (current_time - last_connect_time > CHECK_CONNECT_TIME)){ //마지막 연결 시간으로부터 10초 지났을 때
            ESP_LOGW(WS, "서버와의 연결 끊김. 재연결 시도중...");
            if(client != NULL){ //웹소켓 클라이언트가 존재할 때
                esp_websocket_client_stop(client);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}