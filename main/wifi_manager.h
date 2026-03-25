#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "config.h"

void initialise_wifi(AppConfig *config);

cJSON *wifi_sta_init(bool enabled, char *ssid, char *password);
cJSON *wifi_ap_init(bool enabled, char *ssid, char *password);

#endif // WIFI_MANAGER_H
