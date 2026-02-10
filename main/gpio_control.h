#ifndef GPIO_CONTROL_H
#define GPIO_CONTROL_H

#include <stdint.h>
#include "esp_err.h"

/* LED Strip Configuration */
#define RGB_LED_GPIO 8
#define LED_NUM 1

/**
 * @brief Initialize GPIO for light control
 */
void gpio_control_init(void);

/**
 * @brief Set the light state
 * @param state 0 for OFF, 1 for ON
 * @return ESP_OK on success
 */
esp_err_t gpio_set_light_state(uint8_t state);

/**
 * @brief Initialize RGB LED strip
 */
void rgb_led_init(void);

#endif // GPIO_CONTROL_H
