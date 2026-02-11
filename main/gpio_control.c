#include "gpio_control.h"

#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

/* LED Strip Configuration */
#define RGB_LED_GPIO 8
#define LED_NUM 1

static const char *TAG = "GPIO";

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
}

esp_err_t gpio_set_light_state(uint8_t state) {
    if (state != 0 && state != 1) {
        return ESP_ERR_INVALID_ARG;
    }
    gpio_set_level(GPIO_LIGHT_4, state);
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
