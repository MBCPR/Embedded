#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"
#include "esp_netif.h"

# define MAX_SSID_LEN 32
# define MAX_PASSWORD_LEN 64

/**
 * @brief Wi-Fi 스택을 초기화합니다.
 */
esp_err_t wifi_init(void);

/**
 * @brief Wi-Fi STA 모드를 시작하고 주어진 ssid 와 password 을 이용해 Wi-Fi 액세스 포인트에 연결을 시도합니다.
 */
esp_err_t wifi_mode_set_sta(const char *ssid, const char *password);

/**
 * @brief Wi-Fi SoftAP 모드를 시작하고, HTTP 서버를 시작합니다.
 * SoftAP+STA 모드(WIFI_MODE_APSTA)로 시작하여 스캔이 가능합니다.
 */
esp_err_t wifi_mode_set_apsta(void);

esp_err_t wifi_mode_crush_ap(void);

/**
 * @brief 주어진 자격 증명으로 STA 모드에서 Wi-Fi 액세스 포인트에 연결을 시도합니다.
 */
esp_err_t wifi_connect_sta(const char *ssid, const char *password);

/**
 * @brief SoftAP 모드에서 웹서버를 열어 wifi 데이터를 가져올 준비를 합니다
 */
esp_err_t start_get_wifi(void);

/**
 * @brief 플래그를 확인해서 ssid, password 모두 유효하다면 값을 가져옵니다
 */
esp_err_t check_get_wifi(char *ssid, char *password);

/**
 * @brief 웹서버를 닫습니다
 */
esp_err_t stop_get_wifi(void);

/**
 * @brief 현재 기기가 Wi-Fi에 연결되어 있는지 확인합니다.
 */
esp_err_t wifi_is_connected(void);

/**
 * @brief 현재 기기의 할당된 IP 주소를 확인합니다.
 */
esp_err_t wifi_get_ip_addr(esp_ip4_addr_t *ip_addr);

#endif // WIFI_H