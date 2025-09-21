#include"softAP.h"
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_http_server.h"

#define EXAMPLE_ESP_WIFI_SSID      "MY_ESP32_AP"
#define EXAMPLE_ESP_WIFI_PASS      "password123"
#define EXAMPLE_MAX_STA_CONN       1

static const char *TAG = "SOFTAP_HTTP_SERVER";

static char g_ssid[32];
static char g_password[32];

static bool check_ssid = false;
static bool check_password = false;

// HTTP GET 요청을 처리하는 핸들러
static esp_err_t get_handler(httpd_req_t *req)
{
    char* buf;
    size_t buf_len;

    // URI 쿼리 길이 확인
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char*) malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);

            char param[32];
            // SSID 파라미터 추출 및 출력
            if (httpd_query_key_value(buf, "ssid", param, sizeof(param)) == ESP_OK) {
				strcpy(g_ssid, param);
				check_ssid = true;
                ESP_LOGI(TAG, "SSID: %s", param);
            }
            else {
				check_ssid = false;
			}
			
            // 비밀번호 파라미터 추출 및 출력
            if (httpd_query_key_value(buf, "password", param, sizeof(param)) == ESP_OK) {
				strcpy(g_password, param);
				check_password = true;
                ESP_LOGI(TAG, "Password: %s", param);
            }
            else {
				check_password = false;
			}
        }
        free(buf);
    }
    
    // 응답 전송
    const char* resp_str = "Success";
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

// HTTP 서버 시작 함수
static esp_err_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    // start_webserver() 함수 내에 추가
	httpd_uri_t get_uri = {
    	.uri      = "/connect",
    	.method   = HTTP_GET,
    	.handler  = get_handler,
    	.user_ctx = NULL
	};

	// URI 핸들러 등록
	httpd_register_uri_handler(server, &get_uri);

    ESP_LOGI(TAG, "Starting web server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &get_uri);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Error starting server!");
    return ESP_FAIL;
}

// Wi-Fi 이벤트 핸들러 (연결/연결 해제 로깅)
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station %02x:%02x:%02x:%02x:%02x:%02x join, AID=%u", 
         event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5], event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station %02x:%02x:%02x:%02x:%02x:%02x join, AID=%u", 
         event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5], event->aid);
    }
}

// Wi-Fi SoftAP 초기화 함수
void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = 1,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, 1);
}

// 사용 함수
esp_err_t softap_get_config(char* ssid, char* password)
{
    // WiFi SoftAP 초기화 및 시작
    wifi_init_softap(); 
    
    // 웹 서버 시작
    httpd_handle_t server = start_webserver();
    
    // 웹 요청 대기 루프
    while(!check_ssid || !check_password) {
        // 1초씩 지연
        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
    
    // 요청이 오면 서버를 중단
    stop_webserver(server);
    
    // WiFi 모드 중단
    esp_wifi_stop();
    
    // 전역 변수에서 인자로 받은 버퍼로 데이터 복사
    strcpy(ssid, g_ssid);
    strcpy(password, g_password);
    
    // 다음 사용을 위해 플래그 초기화
    check_ssid = false;
    check_password = false; 
    
    return ESP_OK;
}