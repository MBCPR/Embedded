#include "wifi.h"
#include "URL.h"

#include <string.h>
#include <stdbool.h>
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#ifndef CONFIG_ESP_MAXIMUM_RETRY
#define CONFIG_ESP_MAXIMUM_RETRY 2
#endif

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define EXAMPLE_ESP_WIFI_SSID      "MY_ESP32_AP"
#define EXAMPLE_ESP_WIFI_PASS      "password123"
#define EXAMPLE_MAX_STA_CONN       1

static const char *TAG = "WIFI_CORE";
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static esp_ip4_addr_t s_ip_addr;

// softAP
static char g_ssid[32];
static char g_password[32];
static bool check_ssid = false;
static bool check_password = false;

// STA(임시)
static bool UDP_on = false;


static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }

        wifi_event_sta_disconnected_t* event_info = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGE(TAG, "Wi-Fi disconnected, reason: %d", event_info->reason);
    }
}

// SoftAP 이벤트 핸들러
static void softap_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station %02x:%02x:%02x:%02x:%02x:%02x join, AID=%u",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station %02x:%02x:%02x:%02x:%02x:%02x leave, AID=%u",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
    }
}

// HTTP GET 요청 핸들러
static esp_err_t get_handler(httpd_req_t *req)
{
    char* buf;
    size_t buf_len;

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char*) malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[32];
            if (httpd_query_key_value(buf, "ssid", param, sizeof(param)) == ESP_OK) {
				
				strcpy(g_ssid, param);
				url_decode(g_ssid);
				check_ssid = true;
                ESP_LOGI(TAG, "SSID: %s", g_ssid);
            } else {
				check_ssid = false;
			}
			
            if (httpd_query_key_value(buf, "password", param, sizeof(param)) == ESP_OK) {
				
				strcpy(g_password, param);
				url_decode(g_password);
				check_password = true;
                ESP_LOGI(TAG, "Password: %s", g_password);
            } else {
				check_password = false;
			}
        }
        free(buf);
    }
    
    const char* resp_str = "Success";
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
	const char* resp_str = "OK";
	httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
	
	return ESP_OK;
}
                                
static esp_err_t start_get_handler(httpd_req_t *req)
{
	UDP_on = true;
	const char* resp_str = "OK";
	httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
	
	return ESP_OK;
}

static esp_err_t stop_get_handler(httpd_req_t *req)
{
	UDP_on = false;
	const char* resp_str = "OK";
	httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
	
	return ESP_OK;
}

// HTTP 서버 시작
static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_uri_t get_uri = {
        .uri      = "/connect",
        .method   = HTTP_GET,
        .handler  = get_handler,
        .user_ctx = NULL
    };
    
    httpd_uri_t get_uri_status = {
        .uri      = "/esp32/connect",
        .method   = HTTP_GET,
        .handler  = status_get_handler,
        .user_ctx = NULL
    };
    
    httpd_uri_t get_uri_start = {
        .uri      = "/esp32/RealTime/start",
        .method   = HTTP_GET,
        .handler  = start_get_handler,
        .user_ctx = NULL
    };
    
    httpd_uri_t get_uri_stop = {
        .uri      = "/esp32/RealTime/stop",
        .method   = HTTP_GET,
        .handler  = stop_get_handler,
        .user_ctx = NULL
    };

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &get_uri);
        httpd_register_uri_handler(server, &get_uri_status);
        httpd_register_uri_handler(server, &get_uri_start);
        httpd_register_uri_handler(server, &get_uri_stop);
        return server;
    }

    ESP_LOGE(TAG, "Error starting server!");
    return NULL;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Stopping web server");
    return httpd_stop(server);
}

// 1. 공용 초기화 함수
esp_err_t wifi_init(void) {
    static bool initialized = false;
    if (initialized) {
        ESP_LOGW(TAG, "Wi-Fi stack is already initialized.");
        return ESP_OK;
    }
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &softap_event_handler, NULL, NULL));
    
    initialized = true;
    return ESP_OK;
}

// 2. STA 연결 함수
esp_err_t wifi_connect_sta(const char *ssid, const char *password) {
    // 기존 모드 정지
    esp_wifi_stop();
    s_retry_num = 0; // 재시도 횟수 초기화
    
    esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    strcpy((char*)wifi_config.sta.ssid, ssid);
    strcpy((char*)wifi_config.sta.password, password);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s", ssid);
        // Wi-Fi 연결 성공 후 웹 서버 시작
        httpd_handle_t server = start_webserver();
        if (server == NULL) {
            ESP_LOGE(TAG, "Failed to start web server after STA connection");
            return ESP_FAIL;
        }
        return ESP_OK;
    }
    else {
        ESP_LOGE(TAG, "Failed to connect to SSID:%s", ssid);
        esp_wifi_stop();
        return ESP_FAIL;
    }
}

// 3. SoftAP 모드 함수
esp_err_t softap_get_config(char* ssid, char* password)
{
    esp_wifi_stop();
    esp_netif_create_default_wifi_ap();

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

    httpd_handle_t server = start_webserver();
    if (server == NULL) {
        ESP_LOGE(TAG, "Failed to start web server");
        return ESP_FAIL;
    }
    
    while(!check_ssid || !check_password) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    stop_webserver(server);
    esp_wifi_stop(); // SoftAP 모드 중단
    
    strcpy(ssid, g_ssid);
    strcpy(password, g_password);
    
    check_ssid = false;
    check_password = false; 
    
    return ESP_OK;
}

// 4. 연결 상태 확인 함수
esp_err_t wifi_is_connected(void) {
    wifi_ap_record_t ap_info;
    return esp_wifi_sta_get_ap_info(&ap_info);
}

// 5. IP 주소 획득 함수
esp_err_t wifi_get_ip_addr(esp_ip4_addr_t *ip_addr) {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        if (ip_addr != NULL) {
            *ip_addr = s_ip_addr;
        }
        return ESP_OK;
    }
    return ESP_FAIL;
}