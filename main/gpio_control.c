#include "gpio_control.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "GPIO";

void gpio_control_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_LIGHT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(GPIO_LIGHT, 0);
    ESP_LOGI(TAG, "GPIO %d initialized as output", GPIO_LIGHT);
}

esp_err_t gpio_set_light_state(uint8_t state)
{
    if (state != 0 && state != 1)
    {
        return ESP_ERR_INVALID_ARG;
    }
    gpio_set_level(GPIO_LIGHT, state);
    return ESP_OK;
}

static led_strip_handle_t led_strip = NULL;

/* RGB Color Cycling Task */
static void rgb_led_task(void *pvParameters)
{
    uint8_t color_index = 0;
    const char *color_names[] = {"RED", "GREEN", "BLUE"};

    while (1)
    {
        switch (color_index)
        {
        case 0: // Red
            led_strip_set_pixel(led_strip, 0, 51, 0, 0);
            break;
        case 1: // Green
            led_strip_set_pixel(led_strip, 0, 0, 51, 0);
            break;
        case 2: // Blue
            led_strip_set_pixel(led_strip, 0, 0, 0, 51);
            break;
        }
        led_strip_refresh(led_strip);
        ESP_LOGI(TAG, "LED Color: %s", color_names[color_index]);

        color_index = (color_index + 1) % 3;
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 second delay
    }
}

/* Initialize LED Strip */
void rgb_led_init(void)
{
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
