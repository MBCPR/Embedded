#ifndef STA_H
#define STA_H

#include "esp_err.h"
#include "esp_wifi.h"

#ifndef CONFIG_ESP_MAXIMUM_RETRY
#define CONFIG_ESP_MAXIMUM_RETRY 5
#endif

/**
 * @brief Wi-Fi 스택을 STA 모드로 초기화합니다.
 * STA 함수를 사용하기 전 실행 할 것
 * @return 성공 시 ESP_OK, 실패 시 에러 코드 반환.
 */
esp_err_t wifi_sta_init(void);

/**
 * @brief 주어진 자격 증명을 사용하여 Wi-Fi 액세스 포인트에 연결을 시도합니다.
 * 연결 성공 시 ESP_OK를 반환하며, 실패 시 STA 모드를 비활성화하고 에러 코드를 반환합니다.
 * @param ssid Wi-Fi 네트워크의 SSID.
 * @param password Wi-Fi 네트워크의 비밀번호.
 * @return 성공 시 ESP_OK, 실패 시 에러 코드 반환.
 */
esp_err_t wifi_connect_sta(const char *ssid, const char *password);

/**
 * @brief 현재 기기가 Wi-Fi 액세스 포인트에 연결되어 있는지 확인합니다.
 * @return 연결되어 있으면 ESP_OK, 그렇지 않으면 ESP_FAIL 반환.
 */
esp_err_t wifi_is_connected(void);

/**
 * @brief 현재 기기의 Wi-Fi 액세스 ip 주소를 확인합니다.
 * @return 주소 획득에 성공하면 ESP_OK, 그렇지 않으면 ESP_FAIL 반환.
*/
esp_err_t wifi_get_ip_addr(esp_ip4_addr_t *ip_addr);

#endif // STA_H
