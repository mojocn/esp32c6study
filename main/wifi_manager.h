#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "config.h"

void wifi_init();

void wifi_config_apply(const AppConfig *config);

#endif // WIFI_MANAGER_H
