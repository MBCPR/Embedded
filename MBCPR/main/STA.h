#ifndef STA_H
#define STA_H

#include "esp_err.h"

/**
 * @brief Wi-Fi 스택을 STA 모드로 초기화합니다.
 * 이 함수는 다른 Wi-Fi 함수들을 호출하기 전에 반드시 한 번만 호출해야 합니다.
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

#endif // STA_H
