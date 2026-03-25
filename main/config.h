#ifndef CONFIG_H
#define CONFIG_H

#include "cJSON.h"
#include <stdbool.h>

#define WIFI_MAXIMUM_RETRY 5

/* MQTT Configuration */
#define MQTT_BROKER_HOST "mqtt.shellyiot.cn"
#define MQTT_BROKER_PORT 1883
#define MQTT_USERNAME "admin"
#define MQTT_PASSWORD "ZHou20170928"
#define MQTT_HEARTBEAT_INTERVAL_S 10 /* Heartbeat every 10 seconds */

char *device_name();

typedef struct {
  char *device_name;

  char *wifi_ap_ssid;
  char *wifi_ap_password;
  bool wifi_ap_enabled;

  char *wifi_sta_ssid;
  char *wifi_sta_password;
  bool wifi_sta_enabled;

} AppConfig;

void app_config_free(AppConfig *config);

AppConfig *app_config_from_json(cJSON *json_str);
cJSON *app_config_to_json(const AppConfig *config);

AppConfig *config_init(void);
AppConfig *config_get();
void config_set(const AppConfig *config);

#endif // CONFIG_H
