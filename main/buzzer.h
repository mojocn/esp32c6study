#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>

/* GPIO pin connected to the buzzer (low-level trigger) */
#define BUZZER_PIN 10

/**
 * @brief Initialize the buzzer GPIO (output, idle HIGH = off)
 */
void buzzer_init(void);

/**
 * @brief Turn the buzzer ON (drive GPIO low)
 */
void buzzer_on(void);

/**
 * @brief Turn the buzzer OFF (drive GPIO high)
 */
void buzzer_off(void);

/**
 * @brief Beep for the specified duration then stop
 * @param duration_ms Duration in milliseconds
 */
void buzzer_beep(uint32_t duration_ms);

/**
 * @brief Run a demo: buzzer ON 5 s → OFF 5 s, repeated 4 times (runs in background task)
 */
void buzzer_demo(void);

#endif // BUZZER_H
