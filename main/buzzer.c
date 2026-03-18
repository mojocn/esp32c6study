#include "buzzer.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BUZZER";

/*
 * The module is low-level triggered:
 *   GPIO LOW  → buzzer ON
 *   GPIO HIGH → buzzer OFF (idle state)
 */

void buzzer_init(void) {
    ESP_LOGI(TAG, "Initializing buzzer on GPIO%d (low-level trigger)", BUZZER_PIN);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUZZER_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* Ensure buzzer is off at startup */
    gpio_set_level(BUZZER_PIN, 1);
    ESP_LOGI(TAG, "Buzzer initialized (off)");
}

void buzzer_on(void) {
    gpio_set_level(BUZZER_PIN, 0); /* Active LOW */
}

void buzzer_off(void) {
    gpio_set_level(BUZZER_PIN, 1); /* Idle HIGH */
}

void buzzer_beep(uint32_t duration_ms) {
    buzzer_on();
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    buzzer_off();
}

static void buzzer_demo_task(void *pvParameters) {
    for (int i = 0; i < 4; i++) {
        ESP_LOGI(TAG, "Demo cycle %d/4: ON", i + 1);
        buzzer_on();
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Demo cycle %d/4: OFF", i + 1);
        buzzer_off();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    ESP_LOGI(TAG, "Demo complete");
    vTaskDelete(NULL);
}

void buzzer_demo(void) {
    xTaskCreate(buzzer_demo_task, "buzzer_demo", 2048, NULL, 4, NULL);
}
