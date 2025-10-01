#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"
#include "esp_netif.h"

/**
 * @brief Wi-Fi 스택을 초기화합니다.
 * 이 함수는 모든 Wi-Fi 관련 기능을 사용하기 전에 반드시 한 번만 호출해야 합니다.
 * @return 성공 시 ESP_OK, 실패 시 에러 코드 반환.
 */
esp_err_t wifi_init(void);

/**
 * @brief 주어진 자격 증명으로 STA 모드에서 Wi-Fi 액세스 포인트에 연결을 시도합니다.
 * 연결 성공 시 ESP_OK를, 실패 시 STA 모드를 비활성화하고 에러 코드를 반환합니다.
 * @param ssid Wi-Fi 네트워크의 SSID.
 * @param password Wi-Fi 네트워크의 비밀번호.
 * @return 성공 시 ESP_OK, 실패 시 에러 코드 반환.
 */
esp_err_t wifi_connect_sta(const char *ssid, const char *password);

/**
 * @brief Wi-Fi SoftAP 모드를 시작하고, 사용자로부터 SSID와 비밀번호를 받습니다.
 * @param ssid 사용자 입력 SSID가 저장될 버퍼.
 * @param password 사용자 입력 비밀번호가 저장될 버퍼.
 * @return 성공 시 ESP_OK, 실패 시 에러 코드 반환.
 */
esp_err_t softap_get_config(char* ssid, char* password);

/**
 * @brief 현재 기기가 Wi-Fi에 연결되어 있는지 확인합니다.
 * @return 연결되어 있으면 ESP_OK, 그렇지 않으면 ESP_FAIL 반환.
 */
esp_err_t wifi_is_connected(void);

/**
 * @brief 현재 기기의 할당된 IP 주소를 확인합니다.
 * @param ip_addr 할당된 IP 주소가 저장될 esp_ip4_addr_t 구조체에 대한 포인터.
 * @return 주소 획득에 성공하면 ESP_OK, 그렇지 않으면 에러 코드 반환.
 */
esp_err_t wifi_get_ip_addr(esp_ip4_addr_t *ip_addr);

#endif // WIFI_H