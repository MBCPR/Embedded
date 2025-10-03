#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "esp_websocket_client.h"
#include "esp_event.h"

extern bool finished;
extern esp_websocket_client_handle_t client;
extern uint8_t CapValue;
extern int16_t mpuValueX;
extern int16_t mpuValueY;
extern int16_t mpuValueZ;
extern int64_t last_connect_time;
extern const char *WS;
extern int HX711Value;

void start_websocket_client(void *arg);
void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
void check_server_message(void *arg);

#endif