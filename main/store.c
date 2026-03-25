#include "store.h"

#include "cJSON.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include <stdlib.h>
#include <string.h>

static const char *NAME_SPACE = "store";

void init_nvs(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void store_str_set(const char *key, const char *value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAME_SPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(NAME_SPACE, "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_str(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(NAME_SPACE, "Failed to set key '%s': %s", key, esp_err_to_name(err));
    } else {
        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(NAME_SPACE, "Failed to commit key '%s': %s", key, esp_err_to_name(err));
        }
    }
    nvs_close(handle);
}

char *store_str_get(const char *key) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAME_SPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(NAME_SPACE, "Failed to open NVS for reading: %s", esp_err_to_name(err));
        return NULL;
    }
    size_t required_size = 0;
    err = nvs_get_str(handle, key, NULL, &required_size);
    if (err != ESP_OK) {
        nvs_close(handle);
        return NULL;
    }
    char *out_value = malloc(required_size);
    if (!out_value) {
        ESP_LOGE(NAME_SPACE, "Failed to allocate %zu bytes for key '%s'", required_size, key);
        nvs_close(handle);
        return NULL;
    }
    err = nvs_get_str(handle, key, out_value, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(NAME_SPACE, "Failed to get key '%s': %s", key, esp_err_to_name(err));
        free(out_value);
        out_value = NULL;
    }
    nvs_close(handle);
    return out_value;
}

void store_json_set(const char *key, const cJSON *value) {
    char *str = cJSON_PrintUnformatted(value);
    if (!str) {
        ESP_LOGE(NAME_SPACE, "Failed to serialize JSON for key '%s'", key);
        return;
    }
    store_str_set(key, str);
    free(str);
}

char **store_keys() {

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAME_SPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(NAME_SPACE, "Failed to open NVS for reading keys: %s", esp_err_to_name(err));
        return NULL;
    }

    nvs_iterator_t it = NULL;
    err = nvs_entry_find_in_handle(handle, NVS_TYPE_ANY, &it);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return NULL;
    }
    if (err != ESP_OK) {
        ESP_LOGE(NAME_SPACE, "Failed to find NVS entries: %s", esp_err_to_name(err));
        nvs_close(handle);
        return NULL;
    }

    size_t count = 0;
    size_t capacity = 8;
    char **keys = malloc((capacity + 1) * sizeof(char *));
    if (!keys) {
        ESP_LOGE(NAME_SPACE, "Failed to allocate key list");
        nvs_release_iterator(it);
        nvs_close(handle);
        return NULL;
    }

    while (err == ESP_OK) {
        nvs_entry_info_t info;
        err = nvs_entry_info(it, &info);
        if (err != ESP_OK) {
            ESP_LOGE(NAME_SPACE, "Failed to get entry info: %s", esp_err_to_name(err));
            break;
        }

        size_t key_len = strlen(info.key) + 1;
        char *key_copy = malloc(key_len);
        if (!key_copy) {
            ESP_LOGE(NAME_SPACE, "Failed to allocate key string");
            break;
        }
        memcpy(key_copy, info.key, key_len);

        if (count >= capacity) {
            size_t ncapacity = capacity * 2;
            char **tmp = realloc(keys, (ncapacity + 1) * sizeof(char *));
            if (!tmp) {
                ESP_LOGE(NAME_SPACE, "Failed to grow key list");
                free(key_copy);
                break;
            }
            keys = tmp;
            capacity = ncapacity;
        }

        keys[count++] = key_copy;
        err = nvs_entry_next(&it);
    }

    nvs_release_iterator(it);
    nvs_close(handle);

    if (count == 0) {
        free(keys);
        return NULL;
    }

    keys[count] = NULL;
    return keys;
}

cJSON *store_json_get(const char *key) {
    char *str = store_str_get(key);
    if (!str) {
        return NULL;
    }
    cJSON *value = cJSON_Parse(str);
    if (!value) {
        ESP_LOGE(NAME_SPACE, "Failed to parse JSON for key '%s'", key);
    }
    free(str);
    return value;
}
