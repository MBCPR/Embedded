#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "udp.h"

static const char *TAG = "UDP";

// 전역 상태 변수
static esp_netif_t *sta_netif = NULL;
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static volatile bool send_enabled = true;
static volatile bool start_sent = false;

// 외부 전역 변수 (센서값)
extern volatile int pressure_sensor;
extern volatile int tilt_sensor;
extern volatile int touch_sensor;

// ----------------------------
// 내부 헬퍼 함수
// ----------------------------
static void trim_newline(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) {
        s[n-1] = '\0';
        n--;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected, reconnecting...");
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        start_sent = false;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP");
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ----------------------------
// 공개 API 구현
// ----------------------------
void udp_wifi_init(const char *ssid, const char *pass) {
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_cfg = { 0 };
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid)-1);
    strncpy((char *)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password)-1);

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi init done, connecting...");
}

int udp_socket_init(void) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "소켓 생성 실패");
        return -1;
    }
    ESP_LOGI(TAG, "UDP 소켓 생성됨: %d", sock);
    return sock;
}

int udp_send_start_ip(int sock, const struct sockaddr_in *server_addr) {
    if (!server_addr) return -1;
    esp_netif_ip_info_t ip_info;
    if (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK) {
        char buf[64];
        snprintf(buf, sizeof(buf), "START," IPSTR, IP2STR(&ip_info.ip));
        int ret = sendto(sock, buf, strlen(buf), 0, (const struct sockaddr *)server_addr, sizeof(*server_addr));
        if (ret >= 0) {
            ESP_LOGI(TAG, "START 메시지 전송 완료: %s", buf);
            return 0;
        }
    }
    ESP_LOGW(TAG, "START 메시지 전송 실패");
    return -1;
}

int udp_send_connection_ack(int sock, const struct sockaddr_in *dest_addr) {
    if (!dest_addr) return -1;
    const char *msg = "OK";
    int ret = sendto(sock, msg, strlen(msg), 0, (const struct sockaddr *)dest_addr, sizeof(*dest_addr));
    if (ret >= 0) {
        ESP_LOGI(TAG, "연결 확인 응답(OK) 전송 완료");
        return 0;
    } else {
        ESP_LOGW(TAG, "연결 확인 응답 전송 실패");
        return -1;
    }
}

int udp_send_sensor_data(int sock, const struct sockaddr_in *server_addr) {
    if (!server_addr) return -1;
    char buf[64];
    snprintf(buf, sizeof(buf), "DATA,%d,%d,%d", pressure_sensor, tilt_sensor, touch_sensor);
    int ret = sendto(sock, buf, strlen(buf), 0, (const struct sockaddr *)server_addr, sizeof(*server_addr));
    if (ret < 0) {
        ESP_LOGE(TAG, "센서 데이터 전송 실패");
        return -1;
    }
    ESP_LOGI(TAG, "센서 데이터 전송됨: %s", buf);
    return 0;
}
