#include "gpio_control.h"

#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

/* LED Strip Configuration */
#define RGB_LED_GPIO 8
#define LED_NUM 1

static const char *TAG = "GPIO";

static void gpio_blink_task(void *pvParameters) {
    const gpio_num_t lights[4] = {GPIO_LIGHT_4, GPIO_LIGHT_5, GPIO_LIGHT_6, GPIO_LIGHT_7};

    while (1) {
        uint8_t mode = esp_random() % 6;

        switch (mode) {
            case 0: {
                // Pure chaos: random bitmask every tick
                for (int i = 0; i < 12; i++) {
                    uint8_t mask = esp_random() & 0x0F;
                    for (int j = 0; j < 4; j++)
                        gpio_set_level(lights[j], (mask >> j) & 1);
                    vTaskDelay(pdMS_TO_TICKS(40 + (esp_random() % 120)));
                }
                break;
            }
            case 1: {
                // Random-direction chase, 3 laps
                int dir = esp_random() & 1;
                for (int rep = 0; rep < 3; rep++) {
                    for (int i = 0; i < 4; i++) {
                        int idx = dir ? i : (3 - i);
                        for (int j = 0; j < 4; j++)
                            gpio_set_level(lights[j], j == idx ? 1 : 0);
                        vTaskDelay(pdMS_TO_TICKS(60 + (esp_random() % 100)));
                    }
                }
                break;
            }
            case 2: {
                // Ping-pong bounce
                for (int rep = 0; rep < 4; rep++) {
                    for (int i = 0; i < 4; i++) {
                        for (int j = 0; j < 4; j++)
                            gpio_set_level(lights[j], j == i ? 1 : 0);
                        vTaskDelay(pdMS_TO_TICKS(80 + (esp_random() % 60)));
                    }
                    for (int i = 2; i >= 1; i--) {
                        for (int j = 0; j < 4; j++)
                            gpio_set_level(lights[j], j == i ? 1 : 0);
                        vTaskDelay(pdMS_TO_TICKS(80 + (esp_random() % 60)));
                    }
                }
                break;
            }
            case 3: {
                // Strobe all: rapid random-speed flashing
                int count = 6 + (esp_random() % 8);
                for (int i = 0; i < count; i++) {
                    uint8_t on = i & 1;
                    for (int j = 0; j < 4; j++)
                        gpio_set_level(lights[j], on);
                    vTaskDelay(pdMS_TO_TICKS(40 + (esp_random() % 80)));
                }
                break;
            }
            case 4: {
                // Pair swap with random hold times
                for (int rep = 0; rep < 6; rep++) {
                    gpio_set_level(lights[0], 1);
                    gpio_set_level(lights[1], 0);
                    gpio_set_level(lights[2], 1);
                    gpio_set_level(lights[3], 0);
                    vTaskDelay(pdMS_TO_TICKS(80 + (esp_random() % 140)));
                    gpio_set_level(lights[0], 0);
                    gpio_set_level(lights[1], 1);
                    gpio_set_level(lights[2], 0);
                    gpio_set_level(lights[3], 1);
                    vTaskDelay(pdMS_TO_TICKS(80 + (esp_random() % 140)));
                }
                break;
            }
            case 5: {
                // Binary counter rip
                for (int i = 0; i < 16; i++) {
                    for (int j = 0; j < 4; j++)
                        gpio_set_level(lights[j], (i >> j) & 1);
                    vTaskDelay(pdMS_TO_TICKS(60 + (esp_random() % 80)));
                }
                break;
            }
        }

        // Brief blackout between modes
        for (int j = 0; j < 4; j++)
            gpio_set_level(lights[j], 0);
        ESP_LOGI(TAG, "GPIO crazy cycle done, mode=%d", mode);
        vTaskDelay(pdMS_TO_TICKS(80 + (esp_random() % 120)));
    }
}

void gpio_control_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask =
            (1ULL << GPIO_LIGHT_4) | (1ULL << GPIO_LIGHT_5) | (1ULL << GPIO_LIGHT_6) | (1ULL << GPIO_LIGHT_7),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(GPIO_LIGHT_4, 0);
    gpio_set_level(GPIO_LIGHT_5, 0);
    gpio_set_level(GPIO_LIGHT_6, 0);
    gpio_set_level(GPIO_LIGHT_7, 0);
    ESP_LOGI(TAG, "GPIOs %d, %d, %d, %d initialized as output", GPIO_LIGHT_4, GPIO_LIGHT_5, GPIO_LIGHT_6, GPIO_LIGHT_7);

    xTaskCreate(gpio_blink_task, "gpio_blink_task", 2048, NULL, 5, NULL);
}

esp_err_t gpio_set_light_state(uint8_t state) {
    if (state != 0 && state != 1) {
        return ESP_ERR_INVALID_ARG;
    }
    gpio_set_level(GPIO_LIGHT_4, state);
    gpio_set_level(GPIO_LIGHT_5, state);
    gpio_set_level(GPIO_LIGHT_6, state);
    gpio_set_level(GPIO_LIGHT_7, state);
    return ESP_OK;
}

static led_strip_handle_t led_strip = NULL;

static void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    float hh = h / 60.0f;
    int i = (int)hh;
    float ff = hh - i;

    float p = v * (1.0f - s / 255.0f);
    float q = v * (1.0f - (s / 255.0f) * ff);
    float t = v * (1.0f - (s / 255.0f) * (1.0f - ff));

    switch (i) {
        case 0:
            *r = v;
            *g = t;
            *b = p;
            break;
        case 1:
            *r = q;
            *g = v;
            *b = p;
            break;
        case 2:
            *r = p;
            *g = v;
            *b = t;
            break;
        case 3:
            *r = p;
            *g = q;
            *b = v;
            break;
        case 4:
            *r = t;
            *g = p;
            *b = v;
            break;
        default:
            *r = v;
            *g = p;
            *b = q;
            break;
    }
}

/* Forward declaration */
static void rgb_led_task(void *pvParameters) {
    uint16_t hue = 0;

    while (1) {
        uint8_t r, g, b;
        hsv_to_rgb(hue, 255, 50, &r, &g, &b); // 亮度 50，护眼 😄
        led_strip_set_pixel(led_strip, 0, r, g, b);
        led_strip_refresh(led_strip);
        hue = (hue + 2) % 360; // 控制变化速度
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* Initialize LED Strip */
void rgb_led_init(void) {
    ESP_LOGI(TAG, "Initializing RGB LED on GPIO %d", RGB_LED_GPIO);

    /* LED strip configuration */
    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = LED_NUM,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    /* LED strip RMT configuration */
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));

    /* Clear LED */
    led_strip_clear(led_strip);

    /* Create RGB cycling task */
    xTaskCreate(rgb_led_task, "rgb_led_task", 2048, NULL, 5, NULL);
    ESP_LOGI(TAG, "RGB LED initialized successfully");
}
