#include "gpio_control.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"

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
