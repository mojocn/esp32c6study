#ifndef STORE_H
#define STORE_H

#include "cJSON.h"

/**
 * @brief Initialize NVS (Non-Volatile Storage)
 *
 * Initializes the NVS flash with error recovery. If the NVS partition
 * has no free pages or a new version is found, it will be erased and
 * reinitialized.
 */
void init_nvs(void);

/**
 * @brief Store a string value in NVS.
 *
 * @param key   NVS key (max 15 characters)
 * @param value Null-terminated string to store
 */
void store_str_set(const char *key, const char *value);

/**
 * @brief Retrieve a string value from NVS.
 *
 * @param key NVS key to look up
 * @return    Heap-allocated string (caller must free), or NULL on failure
 */
char *store_str_get(const char *key);

/**
 * @brief Serialize a cJSON object and store it in NVS.
 *
 * @param key   NVS key (max 15 characters)
 * @param value cJSON object to serialize and store
 */
void store_json_set(const char *key, const cJSON *value);

/**
 * @brief Retrieve and parse a cJSON object from NVS.
 *
 * @param key NVS key to look up
 * @return    Parsed cJSON object (caller must cJSON_Delete), or NULL on failure
 */
cJSON *store_json_get(const char *key);

#endif // STORE_H
