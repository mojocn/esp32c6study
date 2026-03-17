#ifndef GPIO_CONTROL_H
#define GPIO_CONTROL_H

#include "esp_err.h"

#include <stdint.h>

#define GPIO_LIGHT_4 4
#define GPIO_LIGHT_5 5
#define GPIO_LIGHT_6 6
#define GPIO_LIGHT_7 7
#define GPIO_LIGHT_9 9

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
