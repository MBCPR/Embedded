#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "MPU6050.h"

extern bool g_finished; 
extern esp_websocket_client_handle_t g_client;
extern uint8_t g_CapValue;
extern int16_t g_mpuValueX;
extern int16_t g_mpuValueY;
extern int16_t g_mpuValueZ;
extern int64_t g_last_connect_time;
extern int g_HX711Value;


extern bool g_check_ssid;
extern bool g_check_password;


extern const char *g_WS; 

void start_websocket_client(void *arg);
void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
void check_server_message(void *arg);

#endif