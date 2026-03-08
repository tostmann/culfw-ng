#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"

void wifi_manager_init(void);
void wifi_manager_get_ip(char* ip);

#endif
