#ifndef MAIN_H
#define MAIN_H

#include "sdkconfig.h"

// Explicitly check for serial profile override
#if defined(PROFILE_SERIAL) || defined(CONFIG_PROFILE_SERIAL)
  #define APP_MATTER_ENABLED 0
  #define APP_WIFI_ENABLED 0
#else
  #if defined(CONFIG_ESP_WIFI_ENABLED) && CONFIG_ESP_WIFI_ENABLED == 1
    #define APP_MATTER_ENABLED 1
    #define APP_WIFI_ENABLED 1
  #elif defined(CONFIG_OPENTHREAD_ENABLED) && CONFIG_OPENTHREAD_ENABLED == 1
    #define APP_MATTER_ENABLED 1
    #define APP_WIFI_ENABLED 0
  #else
    #define APP_MATTER_ENABLED 0
    #define APP_WIFI_ENABLED 0
  #endif
#endif

#endif
