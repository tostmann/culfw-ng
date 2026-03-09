#ifndef MAIN_H
#define MAIN_H

#include "sdkconfig.h"

#if defined(CONFIG_ESP_WIFI_ENABLED) || defined(CONFIG_OPENTHREAD_ENABLED)
#define APP_MATTER_ENABLED 1
#else
#define APP_MATTER_ENABLED 0
#endif

#endif
