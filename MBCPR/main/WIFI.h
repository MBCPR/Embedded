#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"
#include "esp_netif.h"

// SoftAP 전역 변수 extern 선언
extern char g_ssid[32];
extern char g_password[64];
extern bool g_check_ssid;
extern bool g_check_password;

/**
 * @brief Wi-Fi 스택을 초기화합니다.
 */
esp_err_t wifi_init(void);

/**
 * @brief 주어진 자격 증명으로 STA 모드에서 Wi-Fi 액세스 포인트에 연결을 시도합니다.
 */
esp_err_t wifi_connect_sta(const char *ssid, const char *password);

/**
 * @brief Wi-Fi SoftAP 모드를 시작하고, HTTP 서버를 시작합니다.
 * SoftAP+STA 모드(WIFI_MODE_APSTA)로 시작하여 스캔이 가능합니다.
 */
esp_err_t softap_get_wifi(char* ssid, char* password);

/**
 * @brief 현재 기기가 Wi-Fi에 연결되어 있는지 확인합니다.
 */
esp_err_t wifi_is_connected(void);

/**
 * @brief 현재 기기의 할당된 IP 주소를 확인합니다.
 */
esp_err_t wifi_get_ip_addr(esp_ip4_addr_t *ip_addr);

/**
 * @brief 주변 Wi-Fi를 스캔하고 NVS에 저장된 SSID가 있는지 확인 후 연결을 시도합니다.
 * SoftAP 대기 중 자동 복구 로직으로 사용됩니다.
 * @return 저장된 Wi-Fi에 연결 성공 시 ESP_OK, 그 외 ESP_FAIL 반환.
 */
esp_err_t wifi_scan_nvs_check(void);

#endif // WIFI_H