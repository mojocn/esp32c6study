#ifndef STORE_H
#define STORE_H

/**
 * @brief Initialize NVS (Non-Volatile Storage)
 *
 * Initializes the NVS flash with error recovery. If the NVS partition
 * has no free pages or a new version is found, it will be erased and
 * reinitialized.
 */
void init_nvs(void);

#endif // STORE_H
