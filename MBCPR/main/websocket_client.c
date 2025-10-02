#include<stdio.h>
#include<string.h>
#include "freertos/FreeRTOS.h" 
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "websocket_client.h"

#define WEB_SERVER_ADDR           "추가예정"        
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
            ESP_LOGI(WS, "Received=%.*s\n", data->data_len, (char*)data->data_ptr);

            if(data->data_len == 4 && strncmp((char*)data->data_ptr, "stop", 4) == 0){
                finished = true;
            }

            if(data->data_len == 4 && strncmp((char*)data->data_ptr, "ping", 4) == 0){
                last_connect_time = esp_log_timestamp();

                const char *pong_msg = "pong";
                esp_websocket_client_send_text((esp_websocket_client_handle_t)handler_args, pong_msg, strlen(pong_msg), portMAX_DELAY);
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

    client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
    esp_websocket_client_start(client);

    while(!finished){
        if(esp_websocket_client_is_connected(client)){
        char message[100];
        snprintf(message, sizeof(message), "{\"cap\": %d, \"mpu_x\": %d, \"mpu_y\": %d, \"mpu_z\": %d}", CapValue, mpuValueX, mpuValueY, mpuValueZ);
        esp_websocket_client_send_text(client, message, strlen(message), portMAX_DELAY);
        }
        else {
            ESP_LOGW(WS, "웹소켓 서버 연결 대기중...");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    esp_websocket_client_stop(client);
    esp_websocket_client_destroy(client);
    vTaskDelete(NULL);
}

//서버와의 연결 확인
void check_server_message(void *arg){
    while(!finished){
        int64_t current_time = esp_log_timestamp();
        if(esp_websocket_client_is_connected(client) && (current_time - last_connect_time > CHECK_CONNECT_TIME)){
            ESP_LOGW(WS, "서버와의 연결 끊김. 재연결 시도중...");
            if(client != NULL){
                esp_websocket_client_stop(client);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}