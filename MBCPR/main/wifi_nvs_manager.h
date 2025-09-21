#ifndef WIFI_NVS_MANAGER_H
#define WIFI_NVS_MANAGER_H

#include "esp_err.h"

#define MAX_WIFI_CONFIGS 5

/**
 * @brief NVS를 초기화하고, Wi-Fi 데이터 관리를 위한 준비를 합니다.
 * @return esp_err_t ESP_OK인 경우 성공, 그 외 실패
 */
esp_err_t wifi_nvs_manager_init(void);

/**
 * @brief 새로운 Wi-Fi 설정(SSID, password)을 NVS에 저장합니다.
 * 가장 오래된 데이터를 자동으로 삭제하는 기능이 포함되어 있습니다.
 * @param ssid 저장할 Wi-Fi SSID
 * @param password 저장할 Wi-Fi password
 * @return esp_err_t ESP_OK인 경우 성공, 그 외 실패
 */
esp_err_t wifi_nvs_save_config(const char* ssid, const char* password);

/**
 * @brief 가장 최근에 사용된 설정부터 순차적으로 Wi-Fi 데이터를 가져옵니다.
 * 반복적으로 호출하여 다음 데이터를 가져올 수 있습니다.
 * 모든 데이터를 순회한 후에는 ESP_ERR_NVS_NOT_FOUND를 반환합니다.
 * @param ssid_out 검색된 SSID가 저장될 버퍼
 * @param password_out 검색된 비밀번호가 저장될 버퍼
 * @return esp_err_t ESP_OK인 경우 성공, ESP_ERR_NVS_NOT_FOUND인 경우 데이터 없음
 */
esp_err_t wifi_nvs_get_next_config(char* ssid_out, char* password_out);

/**
 * @brief 와이파이 설정 검색 순서를 초기화합니다.
 * 새로운 검색을 시작하고 싶을 때 이 함수를 호출하세요.
 */
void wifi_nvs_reset_search_order(void);

#endif // WIFI_NVS_MANAGER_H