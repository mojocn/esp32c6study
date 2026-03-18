#include "max7219.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAX7219";

/* ---- MAX7219 register addresses ---- */
#define REG_NOOP 0x00
#define REG_DIGIT0 0x01
#define REG_DECODE_MODE 0x09
#define REG_INTENSITY 0x0A
#define REG_SCAN_LIMIT 0x0B
#define REG_SHUTDOWN 0x0C
#define REG_TEST 0x0F

/* ---- Low-level SPI bit-bang ---- */

static void spi_send_byte(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(MAX7219_DIN_PIN, (byte >> i) & 1);
        gpio_set_level(MAX7219_CLK_PIN, 1);
        gpio_set_level(MAX7219_CLK_PIN, 0);
    }
}

static void max7219_write(uint8_t reg, uint8_t data) {
    gpio_set_level(MAX7219_CS_PIN, 0);
    spi_send_byte(reg);
    spi_send_byte(data);
    gpio_set_level(MAX7219_CS_PIN, 1);
}

/* ---- Public API ---- */

void max7219_display(const uint8_t fb[8]) {
    for (int row = 0; row < 8; row++) {
        max7219_write(REG_DIGIT0 + row, fb[row]);
    }
}

void max7219_clear(void) {
    uint8_t blank[8] = {0};
    max7219_display(blank);
}

void max7219_set_brightness(uint8_t level) {
    if (level > 15)
        level = 15;
    max7219_write(REG_INTENSITY, level);
}

/* ---- Snake task ---- */

/*
 * The snake path follows a boustrophedon (snake-scan) route across all 64
 * cells so the 3-light head always has a well-defined next cell to move to.
 *
 * Path order: row 0 left→right, row 1 right→left, row 2 left→right, …
 */
#define PATH_LEN 64
#define SNAKE_LEN 3

static void build_path(uint8_t path_r[PATH_LEN], uint8_t path_c[PATH_LEN]) {
    int idx = 0;
    for (int r = 0; r < 8; r++) {
        if (r % 2 == 0) {
            for (int c = 0; c < 8; c++) {
                path_r[idx] = r;
                path_c[idx] = c;
                idx++;
            }
        } else {
            for (int c = 7; c >= 0; c--) {
                path_r[idx] = r;
                path_c[idx] = c;
                idx++;
            }
        }
    }
}

static void max7219_snake_task(void *pvParameters) {
    uint8_t path_r[PATH_LEN], path_c[PATH_LEN];
    build_path(path_r, path_c);

    int head = 0; /* index into path[] for the leading light */

    while (1) {
        uint8_t fb[8] = {0};
        for (int s = 0; s < SNAKE_LEN; s++) {
            int idx = (head - s + PATH_LEN) % PATH_LEN;
            fb[path_r[idx]] |= (0x80 >> path_c[idx]);
        }
        max7219_display(fb);
        vTaskDelay(pdMS_TO_TICKS(80));
        head = (head + 1) % PATH_LEN;
    }
}

void max7219_init(void) {
    ESP_LOGI(TAG, "Initializing MAX7219 (CS=%d, CLK=%d, DIN=%d)", MAX7219_CS_PIN, MAX7219_CLK_PIN, MAX7219_DIN_PIN);

    /* Configure GPIO pins */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MAX7219_CS_PIN) | (1ULL << MAX7219_CLK_PIN) | (1ULL << MAX7219_DIN_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(MAX7219_CS_PIN, 1);
    gpio_set_level(MAX7219_CLK_PIN, 0);

    /* MAX7219 initialization sequence */
    max7219_write(REG_TEST, 0x00);        /* Normal operation (not test) */
    max7219_write(REG_SCAN_LIMIT, 0x07);  /* Display digits 0-7 */
    max7219_write(REG_DECODE_MODE, 0x00); /* No BCD decode — raw segments */
    max7219_write(REG_INTENSITY, 0x08);   /* Medium brightness */
    max7219_write(REG_SHUTDOWN, 0x01);    /* Normal operation (not shutdown) */

    max7219_clear();

    xTaskCreate(max7219_snake_task, "max7219_snake", 2048, NULL, 4, NULL);
    ESP_LOGI(TAG, "MAX7219 initialized, snake running");
}
