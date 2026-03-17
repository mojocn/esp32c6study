#include "gpio_control.h"

#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_strip_rmt.h"

/* LED Strip Configuration */
#define RGB_LED_GPIO 8
#define LED_NUM 1

static const char *TAG = "GPIO";

static led_strip_handle_t led_strip = NULL;

static void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint8_t region = h / 60;
    uint8_t remainder = (h % 60) * 255 / 60;
    uint8_t p = (uint32_t)v * (255 - s) / 255;
    uint8_t q = (uint32_t)v * (255 - ((uint32_t)s * remainder / 255)) / 255;
    uint8_t t = (uint32_t)v * (255 - ((uint32_t)s * (255 - remainder) / 255)) / 255;
    switch (region) {
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

/* Set N_GPIO regular LEDs via bitmask. RGB LED (GPIO8) is driven independently by rgb_disco_task. */
static void apply_led_mask(const gpio_num_t *lights, int n_gpio, uint32_t mask, uint8_t r, uint8_t g, uint8_t b) {
    (void)r;
    (void)g;
    (void)b;
    for (int j = 0; j < n_gpio; j++)
        gpio_set_level(lights[j], (mask >> j) & 1);
}

static void gpio_blink_task(void *pvParameters) {
    const gpio_num_t lights[5] = {GPIO_LIGHT_4, GPIO_LIGHT_5, GPIO_LIGHT_6, GPIO_LIGHT_7, GPIO_LIGHT_9};
    const int N_GPIO = 5;
    const int N = 6; /* 5 GPIO + 1 WS2812 on GPIO 8 */

    while (1) {
        uint8_t mode = esp_random() % 9;
        uint8_t cr = 80 + (esp_random() % 176);
        uint8_t cg = 80 + (esp_random() % 176);
        uint8_t cb = 80 + (esp_random() % 176);

        switch (mode) {
            case 0: {
                /* Pure chaos: random 6-bit bitmask every tick */
                for (int i = 0; i < 15; i++) {
                    apply_led_mask(lights, N_GPIO, esp_random() & 0x3F, cr, cg, cb);
                    vTaskDelay(pdMS_TO_TICKS(40 + (esp_random() % 120)));
                }
                break;
            }
            case 1: {
                /* Random-direction chase, 3 laps */
                int dir = esp_random() & 1;
                for (int rep = 0; rep < 3; rep++) {
                    for (int i = 0; i < N; i++) {
                        int idx = dir ? i : (N - 1 - i);
                        apply_led_mask(lights, N_GPIO, 1u << idx, cr, cg, cb);
                        vTaskDelay(pdMS_TO_TICKS(60 + (esp_random() % 100)));
                    }
                }
                break;
            }
            case 2: {
                /* Ping-pong bounce */
                for (int rep = 0; rep < 4; rep++) {
                    for (int i = 0; i < N; i++) {
                        apply_led_mask(lights, N_GPIO, 1u << i, cr, cg, cb);
                        vTaskDelay(pdMS_TO_TICKS(80 + (esp_random() % 60)));
                    }
                    for (int i = N - 2; i >= 1; i--) {
                        apply_led_mask(lights, N_GPIO, 1u << i, cr, cg, cb);
                        vTaskDelay(pdMS_TO_TICKS(80 + (esp_random() % 60)));
                    }
                }
                break;
            }
            case 3: {
                /* Strobe all 6 */
                int count = 8 + (esp_random() % 10);
                for (int i = 0; i < count; i++) {
                    apply_led_mask(lights, N_GPIO, (i & 1) ? 0x3F : 0, cr, cg, cb);
                    vTaskDelay(pdMS_TO_TICKS(30 + (esp_random() % 70)));
                }
                break;
            }
            case 4: {
                /* Alternating odd/even: bits 0,2,4 vs bits 1,3,5 */
                for (int rep = 0; rep < 6; rep++) {
                    apply_led_mask(lights, N_GPIO, 0x15, cr, cg, cb);
                    vTaskDelay(pdMS_TO_TICKS(80 + (esp_random() % 140)));
                    apply_led_mask(lights, N_GPIO, 0x2A, cr, cg, cb);
                    vTaskDelay(pdMS_TO_TICKS(80 + (esp_random() % 140)));
                }
                break;
            }
            case 5: {
                /* Binary counter (6 bits = 64 states) */
                for (int i = 0; i < 64; i++) {
                    apply_led_mask(lights, N_GPIO, (uint32_t)i, cr, cg, cb);
                    vTaskDelay(pdMS_TO_TICKS(40 + (esp_random() % 60)));
                }
                break;
            }
            case 6: {
                /* Sparkle: random single flash on dark background */
                for (int i = 0; i < 25; i++) {
                    apply_led_mask(lights, N_GPIO, 1u << (esp_random() % N), cr, cg, cb);
                    vTaskDelay(pdMS_TO_TICKS(30 + (esp_random() % 60)));
                }
                break;
            }
            case 7: {
                /* Fill & drain wave */
                for (int rep = 0; rep < 3; rep++) {
                    uint32_t mask = 0;
                    for (int i = 0; i < N; i++) {
                        mask |= 1u << i;
                        apply_led_mask(lights, N_GPIO, mask, cr, cg, cb);
                        vTaskDelay(pdMS_TO_TICKS(70 + (esp_random() % 80)));
                    }
                    for (int i = 0; i < N; i++) {
                        mask &= ~(1u << i);
                        apply_led_mask(lights, N_GPIO, mask, cr, cg, cb);
                        vTaskDelay(pdMS_TO_TICKS(70 + (esp_random() % 80)));
                    }
                }
                break;
            }
            case 8: {
                /* Rainbow on RGB LED + random GPIO frenzy */
                uint16_t hue = esp_random() % 360;
                for (int i = 0; i < 40; i++) {
                    uint8_t r2, g2, b2;
                    hsv_to_rgb(hue, 255, 180, &r2, &g2, &b2);
                    /* bit N_GPIO = 1: RGB always on, lower bits random */
                    apply_led_mask(lights, N_GPIO, (esp_random() & 0x1F) | (1u << N_GPIO), r2, g2, b2);
                    hue = (hue + 9) % 360;
                    vTaskDelay(pdMS_TO_TICKS(30 + (esp_random() % 50)));
                }
                break;
            }
        }

        /* Brief blackout between modes */
        apply_led_mask(lights, N_GPIO, 0, 0, 0, 0);
        ESP_LOGI(TAG, "GPIO crazy cycle done, mode=%d", mode);
        vTaskDelay(pdMS_TO_TICKS(80 + (esp_random() % 120)));
    }
}

void gpio_control_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_LIGHT_4) | (1ULL << GPIO_LIGHT_5) | (1ULL << GPIO_LIGHT_6) |
                        (1ULL << GPIO_LIGHT_7) | (1ULL << GPIO_LIGHT_9),
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
    gpio_set_level(GPIO_LIGHT_9, 0);
    ESP_LOGI(TAG, "GPIOs %d, %d, %d, %d, %d initialized as output", GPIO_LIGHT_4, GPIO_LIGHT_5, GPIO_LIGHT_6,
             GPIO_LIGHT_7, GPIO_LIGHT_9);

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
    gpio_set_level(GPIO_LIGHT_9, state);
    return ESP_OK;
}

static inline void rgb_set(uint8_t r, uint8_t g, uint8_t b) {
    led_strip_set_pixel(led_strip, 0, r, g, b);
    led_strip_refresh(led_strip);
}

static inline void rgb_off(void) {
    led_strip_clear(led_strip);
}

static void rgb_blink_task(void *pvParameters) {
    while (1) {
        uint8_t mode = esp_random() % 7;

        switch (mode) {
            case 0: {
                /* Rainbow sweep — smooth hue rotation, 3 laps */
                for (int rep = 0; rep < 3; rep++) {
                    for (uint16_t h = 0; h < 360; h += 2) {
                        uint8_t r, g, b;
                        hsv_to_rgb(h, 255, 200, &r, &g, &b);
                        rgb_set(r, g, b);
                        vTaskDelay(pdMS_TO_TICKS(8));
                    }
                }
                break;
            }
            case 1: {
                /* Breathing — smooth fade in/out, hue shifts each breath */
                uint16_t hue = esp_random() % 360;
                for (int rep = 0; rep < 5; rep++) {
                    for (int v = 0; v <= 255; v += 5) {
                        uint8_t r, g, b;
                        hsv_to_rgb(hue, 255, (uint8_t)v, &r, &g, &b);
                        rgb_set(r, g, b);
                        vTaskDelay(pdMS_TO_TICKS(10));
                    }
                    for (int v = 255; v >= 0; v -= 5) {
                        uint8_t r, g, b;
                        hsv_to_rgb(hue, 255, (uint8_t)v, &r, &g, &b);
                        rgb_set(r, g, b);
                        vTaskDelay(pdMS_TO_TICKS(10));
                    }
                    hue = (hue + 60) % 360;
                }
                break;
            }
            case 2: {
                /* Fire flicker — red/orange/yellow with random intensity */
                for (int i = 0; i < 60; i++) {
                    uint8_t v = 120 + (esp_random() % 136);
                    uint8_t s = 180 + (esp_random() % 76);
                    uint16_t h = esp_random() % 30; /* 0–29: deep red to orange */
                    uint8_t r, g, b;
                    hsv_to_rgb(h, s, v, &r, &g, &b);
                    rgb_set(r, g, b);
                    vTaskDelay(pdMS_TO_TICKS(25 + (esp_random() % 55)));
                }
                break;
            }
            case 3: {
                /* Color strobe — saturated random flashes with dark gaps */
                for (int i = 0; i < 20; i++) {
                    uint16_t h = esp_random() % 360;
                    uint8_t r, g, b;
                    hsv_to_rgb(h, 255, 255, &r, &g, &b);
                    rgb_set(r, g, b);
                    vTaskDelay(pdMS_TO_TICKS(35 + (esp_random() % 40)));
                    rgb_off();
                    vTaskDelay(pdMS_TO_TICKS(20 + (esp_random() % 50)));
                }
                break;
            }
            case 4: {
                /* Meteor — instant bright flash then slow exponential fade */
                for (int rep = 0; rep < 8; rep++) {
                    uint16_t hue = esp_random() % 360;
                    uint8_t r, g, b;
                    hsv_to_rgb(hue, 220, 255, &r, &g, &b);
                    rgb_set(r, g, b);
                    vTaskDelay(pdMS_TO_TICKS(20));
                    for (int v = 255; v > 8; v = v * 7 / 8) {
                        hsv_to_rgb(hue, 220, (uint8_t)v, &r, &g, &b);
                        rgb_set(r, g, b);
                        vTaskDelay(pdMS_TO_TICKS(14));
                    }
                    rgb_off();
                    vTaskDelay(pdMS_TO_TICKS(60 + (esp_random() % 160)));
                }
                break;
            }
            case 5: {
                /* Pulse wave — brightness throbs at variable speed, hue drifts */
                uint16_t hue = esp_random() % 360;
                uint32_t spd = 12 + (esp_random() % 22);
                for (int rep = 0; rep < 6; rep++) {
                    for (int v = 0; v <= 255; v += 5) {
                        uint8_t r, g, b;
                        hsv_to_rgb(hue, 220, (uint8_t)v, &r, &g, &b);
                        rgb_set(r, g, b);
                        vTaskDelay(pdMS_TO_TICKS(spd));
                    }
                    for (int v = 255; v >= 0; v -= 5) {
                        uint8_t r, g, b;
                        hsv_to_rgb(hue, 220, (uint8_t)v, &r, &g, &b);
                        rgb_set(r, g, b);
                        vTaskDelay(pdMS_TO_TICKS(spd));
                    }
                    hue = (hue + 37) % 360;
                }
                break;
            }
            case 6: {
                /* Disco sparkle — rapid saturated bursts on a dark background */
                for (int i = 0; i < 35; i++) {
                    uint16_t hue = esp_random() % 360;
                    uint8_t r, g, b;
                    hsv_to_rgb(hue, 255, 255, &r, &g, &b);
                    rgb_set(r, g, b);
                    vTaskDelay(pdMS_TO_TICKS(15 + (esp_random() % 35)));
                    rgb_off();
                    vTaskDelay(pdMS_TO_TICKS(10 + (esp_random() % 90)));
                }
                break;
            }
        }

        /* Brief blackout between modes */
        rgb_off();
        ESP_LOGI(TAG, "RGB effect done, mode=%d", mode);
        vTaskDelay(pdMS_TO_TICKS(120 + (esp_random() % 180)));
    }
}

/* Initialize LED Strip */
void rgb_led_init(void) {
    ESP_LOGI(TAG, "Initializing RGB LED on GPIO %d", RGB_LED_GPIO);

    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = LED_NUM,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, /* 10 MHz */
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
    ESP_LOGI(TAG, "RGB LED initialized successfully");

    xTaskCreate(rgb_blink_task, "rgb_blink_task", 2048, NULL, 5, NULL);
}
