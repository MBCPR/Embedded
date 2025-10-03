#include "wifi.h"
#include "URL.h"
#include "websocket_client.h"
#include "wifi_nvs_manager.h"

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
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static esp_ip4_addr_t s_ip_addr;

// SoftAP 전역 변수
char g_ssid[32];
char g_password[64];
bool g_check_ssid = false;
bool g_check_password = false;

// HTTP 서버 핸들 
static httpd_handle_t s_server = NULL; 

static esp_err_t stop_webserver(httpd_handle_t server); // 전방 선언

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "IP 주소 획득:" IPSTR, IP2STR(&event->ip_info.ip));
        s_ip_addr = event->ip_info.ip;
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if(g_client != NULL && !esp_websocket_client_is_connected(g_client)) {
            esp_websocket_client_start(g_client);
            ESP_LOGI(g_WS, "웹소켓 재연결 시도중...");
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		
        if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Wi-Fi 재연결 시도 중... (%d/%d)", s_retry_num, CONFIG_ESP_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Wi-Fi 재연결 시도 횟수 초과. 연결 실패.");
            
            // 재연결 최종 실패 시 시스템 재시작 (app_main의 SoftAP 로직으로 복귀)
            ESP_LOGW(TAG, "5초 후 시스템을 재시작하여 SoftAP 모드로 복구합니다.");
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_restart(); 
        }

        wifi_event_sta_disconnected_t* event_info = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGE(TAG, "Wi-Fi 연결에 실패했습니다, 이유: %d", event_info->reason);
    }
}

// SoftAP 이벤트 핸들러
static void softap_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station %02x:%02x:%02x:%02x:%02x:%02x join, AID=%u",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station %02x:%02x:%02x:%02x:%02x:%02x leave, AID=%u",
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
        buf = (char*) malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[64];
            
            if (httpd_query_key_value(buf, "ssid", param, sizeof(param)) == ESP_OK) {
				strcpy(g_ssid, param);
				url_decode(g_ssid);
				g_check_ssid = true;
                ESP_LOGI(TAG, "SSID: %s", g_ssid);
            } else {
				g_check_ssid = false;
			}
			
            if (httpd_query_key_value(buf, "password", param, sizeof(param)) == ESP_OK) {
				strcpy(g_password, param);
				url_decode(g_password);
				g_check_password = true;
                ESP_LOGI(TAG, "Password: %s", g_password);
            } else {
				g_check_password = false;
			}
            
            // 설정 수신 후 SoftAP 종료
            stop_webserver(s_server);
            s_server = NULL;
            esp_wifi_stop(); 
        }
        free(buf);
    }
    
    const char* resp_str = "Success";
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

// HTTP 서버 시작
static httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

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

    ESP_LOGE(TAG, "Error starting server!");
    return NULL;
}

static esp_err_t stop_webserver(httpd_handle_t server) {
    if (server == NULL && s_server != NULL) {
        server = s_server;
        s_server = NULL;
    }
    if (server == NULL) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "웹 서버 정지");
    return httpd_stop(server);
}

// 1. 공용 초기화 함수
esp_err_t wifi_init(void) {
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
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = 0;
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = 0;


    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi 연결에 성공했습니다, SSID:%s", ssid);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "WiFi에 연결할 수 없습니다, SSID:%s", ssid);
        esp_wifi_stop();
        return ESP_FAIL;
    }
}

// 3. SoftAP 모드 함수 (APSTA 모드 활성화)
esp_err_t softap_get_wifi(char* ssid, char* password) {
    // SoftAP가 이미 실행 중이면 아무것도 하지 않음 
    if (s_server != NULL) {
        return ESP_OK; 
    }

    esp_wifi_stop();
    
    // AP와 STA를 동시에 활성화 (SoftAP 중 스캔 가능)
    esp_netif_create_default_wifi_sta();
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

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 웹 서버 시작
    httpd_handle_t server = start_webserver();
    if (server == NULL) {
        ESP_LOGE(TAG, "웹 서버를 여는데 실패했습니다");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

// 4. 연결 상태 확인 함수
esp_err_t wifi_is_connected(void) {
    wifi_ap_record_t ap_info;
    return esp_wifi_sta_get_ap_info(&ap_info);
}

// 5. IP 주소 획득 함수
esp_err_t wifi_get_ip_addr(esp_ip4_addr_t *ip_addr) {
    if (wifi_is_connected() == ESP_OK) {
        if (ip_addr != NULL) {
            *ip_addr = s_ip_addr;
        }
        return ESP_OK;
    }
    return ESP_FAIL;
}

// 6. 주변 Wi-Fi를 스캔하고 NVS에 저장된 SSID가 있는지 확인 후 연결 시도
esp_err_t wifi_scan_nvs_check(void) {
    uint16_t ap_count = 0;
    char nvs_ssid[32];
    char nvs_password[64];
    
    // 1. 스캔 옵션 설정 및 스캔 시작
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false
    };
    esp_err_t scan_ret = esp_wifi_scan_start(&scan_config, true); 
    if (scan_ret != ESP_OK) return ESP_FAIL;
    
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    
    if (ap_count == 0) {
        return ESP_FAIL;
    }

    wifi_ap_record_t *ap_info = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (ap_info == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_info));

    // 2. NVS 설정과 스캔된 AP 비교
    wifi_nvs_reset_search_order();
    while (wifi_nvs_get_next_config(nvs_ssid, nvs_password) == ESP_OK) {
        for (int i = 0; i < ap_count; i++) {
            // 저장된 SSID와 스캔된 AP의 SSID를 비교
            if (strcmp((char *)ap_info[i].ssid, nvs_ssid) == 0) {
                ESP_LOGI(TAG, "NVS에 저장된 Wi-Fi (%s) 발견! SoftAP 중단 및 STA 연결 시도.", nvs_ssid);
                free(ap_info);

                // SoftAP 중단 (웹서버와 AP 모드를 중단)
                stop_webserver(s_server); 
                
                // STA 연결 시도
                if (wifi_connect_sta(nvs_ssid, nvs_password) == ESP_OK) {
                    wifi_nvs_save_config(nvs_ssid, nvs_password);
                    return ESP_OK; // 연결 성공
                } else {
                    ESP_LOGW(TAG, "자동 연결 실패. SoftAP 대기를 재개합니다.");
                    return ESP_FAIL; // 연결 실패 (main.c에서 SoftAP 재개)
                }
            }
        }
    }
    
    free(ap_info);
    return ESP_FAIL; // NVS에 저장된 AP를 찾지 못함
}