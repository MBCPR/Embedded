#include "wifi_nvs_manager.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <time.h>
#include <stdlib.h>

static const char* TAG = "WIFI_NVS_MANAGER";
static int8_t s_search_index = -1; // 검색 순서 관리를 위한 정적 변수

typedef struct {
    int index;
    time_t timestamp;
} wifi_timestamp_t;

static int compare_timestamps(const void* a, const void* b) {
    const wifi_timestamp_t* ts_a = (const wifi_timestamp_t*)a;
    const wifi_timestamp_t* ts_b = (const wifi_timestamp_t*)b;
    return (ts_b->timestamp - ts_a->timestamp);
}

static esp_err_t cleanup_oldest_data(nvs_handle_t handle, uint32_t current_count) {
    if (current_count <= MAX_WIFI_CONFIGS) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Wi-Fi 데이터가 %d개를 초과하여 가장 오래된 데이터를 삭제합니다.", MAX_WIFI_CONFIGS);

    time_t oldest_time = time(NULL);
    int oldest_index = -1;
    for (int i = 0; i < current_count; i++) {
        char key_ts[20];
        int64_t last_used;
        snprintf(key_ts, sizeof(key_ts), "ts_%d", i);
        if (nvs_get_i64(handle, key_ts, &last_used) == ESP_OK) {
            if (last_used < oldest_time) {
                oldest_time = last_used;
                oldest_index = i;
            }
        }
    }
    if (oldest_index != -1) {
        char key_ssid[20], key_password[20], key_ts[20];
        snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", oldest_index);
        snprintf(key_password, sizeof(key_password), "password_%d", oldest_index);
        snprintf(key_ts, sizeof(key_ts), "ts_%d", oldest_index);
        nvs_erase_key(handle, key_ssid);
        nvs_erase_key(handle, key_password);
        nvs_erase_key(handle, key_ts);
        ESP_LOGI(TAG, "인덱스 %d의 Wi-Fi 데이터를 삭제했습니다.", oldest_index);

        uint32_t last_index = current_count - 1;
        if (oldest_index != last_index) {
            char old_ssid[32], old_password[64];
            int64_t old_ts;
            size_t ssid_len = sizeof(old_ssid), password_len = sizeof(old_password);
            snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", last_index);
            snprintf(key_password, sizeof(key_password), "password_%d", last_index);
            snprintf(key_ts, sizeof(key_ts), "ts_%d", last_index);
            nvs_get_str(handle, key_ssid, old_ssid, &ssid_len);
            nvs_get_str(handle, key_password, old_password, &password_len);
            nvs_get_i64(handle, key_ts, &old_ts);

            snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", oldest_index);
            nvs_set_str(handle, key_ssid, old_ssid);
            snprintf(key_password, sizeof(key_password), "password_%d", oldest_index);
            nvs_set_str(handle, key_password, old_password);
            snprintf(key_ts, sizeof(key_ts), "ts_%d", oldest_index);
            nvs_set_i64(handle, key_ts, old_ts);

            snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", last_index);
            snprintf(key_password, sizeof(key_password), "password_%d", last_index);
            snprintf(key_ts, sizeof(key_ts), "ts_%d", last_index);
            nvs_erase_key(handle, key_ssid);
            nvs_erase_key(handle, key_password);
            nvs_erase_key(handle, key_ts);
        }
        nvs_set_u32(handle, "wifi_count", current_count - 1);
        nvs_commit(handle);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t wifi_nvs_manager_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t wifi_nvs_save_config(const char* ssid, const char* password) {
    if (!ssid || !password) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("wifi_data", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    uint32_t wifi_count = 0;
    nvs_get_u32(handle, "wifi_count", &wifi_count);
    int existing_index = -1;
    for(int i=0; i<wifi_count; i++){
        char key_ssid[20];
        char stored_ssid[32];
        size_t len = sizeof(stored_ssid);
        snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", i);
        err = nvs_get_str(handle, key_ssid, NULL, &len);
        if (err == ESP_OK) {
            err = nvs_get_str(handle, key_ssid, stored_ssid, &len);
            if(err == ESP_OK && strcmp(ssid, stored_ssid) == 0){
                existing_index = i;
                break;
            }
        }
    }
    if(existing_index != -1){
        char key_ts[20];
        snprintf(key_ts, sizeof(key_ts), "ts_%d", existing_index);
        nvs_set_i64(handle, key_ts, time(NULL));
        ESP_LOGI(TAG, "기존 Wi-Fi 데이터(%s)의 타임스탬프를 업데이트했습니다.", ssid);
    } else {
        char key_ssid[20], key_password[20], key_ts[20];
        int new_index = wifi_count;
        snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", new_index);
        snprintf(key_password, sizeof(key_password), "password_%d", new_index);
        snprintf(key_ts, sizeof(key_ts), "ts_%d", new_index);
        nvs_set_str(handle, key_ssid, ssid);
        nvs_set_str(handle, key_password, password);
        nvs_set_i64(handle, key_ts, time(NULL));
        wifi_count++;
        nvs_set_u32(handle, "wifi_count", wifi_count);
        ESP_LOGI(TAG, "새로운 Wi-Fi 데이터(%s)를 저장했습니다. (총 %d개)", ssid, wifi_count);
    }
    err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        nvs_open("wifi_data", NVS_READWRITE, &handle);
        cleanup_oldest_data(handle, wifi_count);
        nvs_close(handle);
    }
    return err;
}

esp_err_t wifi_nvs_get_next_config(char* ssid_out, char* password_out) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("wifi_data", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    uint32_t wifi_count = 0;
    nvs_get_u32(handle, "wifi_count", &wifi_count);
    if (wifi_count == 0) {
        nvs_close(handle);
        return ESP_ERR_NVS_NOT_FOUND;
    }
    static wifi_timestamp_t sorted_configs[MAX_WIFI_CONFIGS];
    static int num_found = 0;
    if (s_search_index == -1) {
        num_found = 0;
        for (int i = 0; i < wifi_count; i++) {
            char key_ts[20];
            int64_t last_used;
            snprintf(key_ts, sizeof(key_ts), "ts_%d", i);
            if (nvs_get_i64(handle, key_ts, &last_used) == ESP_OK) {
                if (num_found < MAX_WIFI_CONFIGS) {
                    sorted_configs[num_found].index = i;
                    sorted_configs[num_found].timestamp = last_used;
                    num_found++;
                }
            }
        }
        qsort(sorted_configs, num_found, sizeof(wifi_timestamp_t), compare_timestamps);
        s_search_index = 0;
    }
    if (s_search_index >= num_found) {
        s_search_index = -1;
        nvs_close(handle);
        return ESP_ERR_NVS_NOT_FOUND;
    }
    char key_ssid[20], key_password[20];
    int current_index = sorted_configs[s_search_index].index;
    size_t ssid_len = 32, password_len = 64;
    snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", current_index);
    snprintf(key_password, sizeof(key_password), "password_%d", current_index);
    err = nvs_get_str(handle, key_ssid, ssid_out, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(handle, key_password, password_out, &password_len);
        s_search_index++;
    }
    nvs_close(handle);
    return err;
}

void wifi_nvs_reset_search_order(void) {
    s_search_index = -1;
    ESP_LOGI(TAG, "Wi-Fi 검색 순서를 초기화했습니다.");
}