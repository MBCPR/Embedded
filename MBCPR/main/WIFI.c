#include "wifi.h"
#include "URL.h"
#include "esp_err.h"
#include "websocket_client.h"

#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "esp_websocket_client.h"
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
static const char *WEB = "WIFI_WEB_SERVER";
static const char *STA = "WIFI_STA_MODE";
static const char *APSTA = "WIFI_APSTA_MODE";
static const char *De = "Debug";


static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static esp_ip4_addr_t s_ip_addr;

// SoftAP 전역 변수
char g_ssid[MAX_SSID_LEN];
char g_password[MAX_PASSWORD_LEN];
bool g_check_ssid = false;
bool g_check_password = false;

// HTTP 서버 핸들 
static httpd_handle_t s_server = NULL; 

static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;

static esp_err_t stop_webserver(httpd_handle_t server);

// wifi 설정 삭제(초기화)
static void destroy_all_netifs() {
	ESP_LOGE(De, "destroy_all_netifs 실행");
    if (s_sta_netif) {
        esp_netif_destroy(s_sta_netif);
        s_sta_netif = NULL;
        ESP_LOGI(TAG, "STA Netif 파괴됨");
    }
    if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
        ESP_LOGI(TAG, "AP Netif 파괴됨");
    }
}


// wifi 연결, 연결 해제, ip 획득 이벤트를 감지해 실행
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
	
	// STA 모드로 wifi에 연결 성공해 ip 주소를 받았을 경우
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		
		ESP_LOGE(De, "event_id == IP_EVENT_STA_GOT_IP 실행");
		
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(STA, "IP 주소 획득:" IPSTR, IP2STR(&event->ip_info.ip));
        s_ip_addr = event->ip_info.ip;
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        
        if(g_client != NULL && !esp_websocket_client_is_connected(g_client)) {
            esp_websocket_client_start(g_client);
            ESP_LOGI(g_WS, "웹소켓 재연결 시도중...");
        }
        
    }// wifi 연결이 해제되었을 경우(접속 단절)
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
	
		ESP_LOGE(De, "event_id == WIFI_EVENT_STA_DISCONNECTED 실행");
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            
        wifi_event_sta_disconnected_t* event_info = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGE(STA, "Wi-Fi 연결에 실패했습니다, 이유: %d", event_info->reason);
            
        // STA모드에서 재연결 최종 실패 시 시스템 재시작
        if(s_ap_netif == NULL){
			ESP_LOGW(STA, "5초 후 시스템을 재시작하여 SoftAP 모드로 복구합니다.");
          	vTaskDelay(pdMS_TO_TICKS(5000));
           	esp_restart();
		}
		else {
			ESP_LOGW(APSTA, "해당 데이터로 연결에 실패했습니다. 다른 와이파이 데이터를 대기합니다.");
		}
    }
}

// SoftAP에 기기가 연결, 연결 해제 이벤트를 감지해 실행
static void softap_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
	// 기기가 연결되었을 경우
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(APSTA, "station %02x:%02x:%02x:%02x:%02x:%02x join, AID=%u",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
    }// 기기가 연결 해제되었을 경우
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(APSTA, "station %02x:%02x:%02x:%02x:%02x:%02x leave, AID=%u",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
    }
}

// HTTP GET 요청 핸들러
static esp_err_t get_handler(httpd_req_t *req) {
    char* buf;
    size_t buf_len;

    buf_len = httpd_req_get_url_query_len(req) + 1;
	
    if (buf_len > 1) {
		
		// 저장공간 할당
        buf = (char*) malloc(buf_len);
        
        // 요청 URL이 유효할 경우
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[64];
            
            ESP_LOGE(De, "get 핸들러 실행");
            
            // ssid 데이터가 있다면 실행
            if (httpd_query_key_value(buf, "ssid", param, sizeof(param)) == ESP_OK) {
				strcpy(g_ssid, param);
				url_decode(g_ssid);
				g_check_ssid = true;
                ESP_LOGI(WEB, "SSID: %s", g_ssid);
            }
            else {
				g_check_ssid = false;
			}
			
			// password 데이터가 있을 경우 실행
            if (httpd_query_key_value(buf, "password", param, sizeof(param)) == ESP_OK) {
				strcpy(g_password, param);
				url_decode(g_password);
				g_check_password = true;
                ESP_LOGI(WEB, "Password: %s", g_password);
            }
            else {
				g_check_password = false;
			}
        }
        // 임시 할당 해제
        free(buf);
    }
    
    //확인 request
    const char* resp_str = "Success";
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

// HTTP 서버 시작
static httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    ESP_LOGE(De, "start_webserver 실행");

    httpd_uri_t get_uri = {
        .uri      = "/connect",
        .method   = HTTP_GET,
        .handler  = get_handler,
        .user_ctx = NULL
    };

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &get_uri);
        s_server = server;
        return server;
    }

    ESP_LOGE(WEB, "Error starting server!");
    return NULL;
}

// HTTP 서버 정지
static esp_err_t stop_webserver(httpd_handle_t server) {
	
	ESP_LOGE(De, "stop_webserver 실행");
	
    if (server == NULL && s_server != NULL) {
        server = s_server;
        s_server = NULL;
    }
    if (server == NULL) {
        return ESP_FAIL;
    }
    ESP_LOGI(WEB, "웹 서버 정지");
    return httpd_stop(server);
}

// 1. 공용 wifi 초기화 함수
esp_err_t wifi_init(void) {
	
	ESP_LOGE(De, "wifi_init 실행");
	
    static bool initialized = false;
    if (initialized) {
        ESP_LOGW(TAG, "이미 초기화되었습니다");
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
    ESP_LOGW(TAG, "WiFi 초기화에 성공했습니다");
    return ESP_OK;
}

// 2. STA 모드 활성화 함수(연결 유지 상태)
esp_err_t wifi_mode_set_sta(const char *ssid, const char *password){
	
	ESP_LOGE(De, "wifi_mode_set_sta 실행");
	// 기존 모드 정지
    esp_wifi_stop();
    
    destroy_all_netifs();
    
    s_sta_netif = esp_netif_create_default_wifi_sta(); // <--- s_sta_netif에 할당
    if (s_sta_netif == NULL) return ESP_FAIL;

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = 0;
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = 0;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
    
    xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    
    ESP_LOGI(STA, "STA 모드 활성화 성공");
    
    return ESP_OK;
}

// 3. APSTA 모드 활성화 함수(연결 끊김 상태)
esp_err_t wifi_mode_set_apsta(void){
	
	ESP_LOGE(De, "wifi_mode_set_apsta 실행");
	
	// 기존 모드 정지
    esp_wifi_stop();
    
    s_retry_num = 0; // 재시도 횟수 초기화
    
    destroy_all_netifs();
    
    s_sta_netif = esp_netif_create_default_wifi_sta(); // <--- s_sta_netif에 할당
    s_ap_netif = esp_netif_create_default_wifi_ap();   // <--- s_ap_netif에 할당
    if (s_sta_netif == NULL || s_ap_netif == NULL) return ESP_FAIL;

    wifi_config_t wifi_ap_config = {
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
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    wifi_config_t wifi_sta_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(APSTA, "AP, STA 모드 활성화 성공");
    
    return ESP_OK;
}

esp_err_t wifi_mode_crush_ap(void){
	
	ESP_LOGE(De, "wifi_mode_crush_ap 실행");
	
	esp_wifi_set_mode(WIFI_MODE_STA);
	
	if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
        ESP_LOGI(TAG, "AP Netif 파괴됨");
    }
    
    return ESP_OK;
}

// 4. STA 연결 함수(연결 시도)
esp_err_t wifi_connect_sta(const char *ssid, const char *password) {
	
	ESP_LOGE(De, "wifi_connect_sta 실행");
	
	xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
	
	esp_err_t ret;
	
	if (s_sta_netif == NULL){
		ESP_LOGE(TAG, "STA 모드가 활성화되지 않았습니다");
		return ESP_FAIL;
	}
	
	wifi_config_t wifi_config = { 
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = 0;
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = 0;
	
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    ret = esp_wifi_connect();
	
	ESP_LOGI(TAG, "WiFi 연결 결과 로그: %s", esp_err_to_name(ret));
	
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi 연결에 성공했습니다, SSID:%s", ssid);
        return ESP_OK;
    }
    else {
        ESP_LOGE(TAG, "WiFi에 연결할 수 없습니다, SSID:%s", ssid);
        esp_wifi_disconnect();
        return ESP_FAIL;
    }
    
}

// 5. wifi 데이터를 받아올 서버 시작
esp_err_t start_get_wifi(void) {
	
	ESP_LOGE(De, "start_get_wifi 실행");
	
    httpd_handle_t server = start_webserver();
    if (server == NULL) {
        ESP_LOGE(TAG, "웹 서버를 여는데 실패했습니다");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

// 6. wifi 데이터가 들어왔는지 확인하고 들어왔다면 저장
esp_err_t check_get_wifi(char *ssid, char *password){
	
	ESP_LOGE(De, "check_get_wifi 실행");
	
	if(g_check_ssid && g_check_password){
		strcpy(ssid, g_ssid);
		strcpy(password, g_password);
		
		g_check_ssid = false;
		g_check_password = false;
		return ESP_OK;
	}
	
	return ESP_FAIL;
}

// 7. wifi 데이터를 받아올 서버를 정지(받아오기 성공)
esp_err_t stop_get_wifi(void){
	
	ESP_LOGE(De, "stop_get_wifi 실행");
	
	if(s_server != NULL){
		stop_webserver(s_server);
		s_server = NULL;
		return ESP_OK;
	}
	return ESP_FAIL;
}

// 8. 연결 상태 확인 함수
esp_err_t wifi_is_connected(void) {
    wifi_ap_record_t ap_info;
    return esp_wifi_sta_get_ap_info(&ap_info);
}

// 9. IP 주소 획득 함수
esp_err_t wifi_get_ip_addr(esp_ip4_addr_t *ip_addr) {
    if (wifi_is_connected() == ESP_OK) {
        if (ip_addr != NULL) {
            *ip_addr = s_ip_addr;
        }
        return ESP_OK;
    }
    return ESP_FAIL;
}