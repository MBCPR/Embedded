#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"

#define HX711_DT  19
#define HX711_SCK 18

// HX711 초기화
void hx711_init(void) {
    gpio_reset_pin(HX711_DT);
    gpio_reset_pin(HX711_SCK);
    gpio_set_direction(HX711_DT, GPIO_MODE_INPUT);
    gpio_set_direction(HX711_SCK, GPIO_MODE_OUTPUT);
    gpio_set_level(HX711_SCK, 0);
}

// HX711에서 24비트 데이터 읽기
long hx711_read(void) {
    long value = 0;
    while (gpio_get_level(HX711_DT) == 1) {
        vTaskDelay(1 / portTICK_PERIOD_MS); // 데이터 준비될 때까지 대기
    }

    for (int i = 0; i < 24; i++) {
        gpio_set_level(HX711_SCK, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
        value = (value << 1) | gpio_get_level(HX711_DT);
        gpio_set_level(HX711_SCK, 0);
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // 채널/증폭 설정용 추가 클럭 (128 gain)
    gpio_set_level(HX711_SCK, 1);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(HX711_SCK, 0);
    vTaskDelay(pdMS_TO_TICKS(1));

    // 부호 비트 처리 (24비트 signed 변환)
    if (value & 0x800000) {
        value |= ~0xFFFFFF;
    }
    
    value += 200000;
    value *= 0.0001;
    value /= 2;

    return value;
}

void app_main(void) {
    hx711_init();
    while (1) {
        long raw = hx711_read();
        printf("HX711 raw: %ld\n", raw);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}