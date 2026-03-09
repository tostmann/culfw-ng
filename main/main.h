#ifndef MAIN_H
#define MAIN_H

#include "sdkconfig.h"

#if defined(CONFIG_ESP_WIFI_ENABLED)
  #if CONFIG_ESP_WIFI_ENABLED == 1
    #define APP_MATTER_ENABLED 1
    #warning "MATTER ENABLED DUE TO CONFIG_ESP_WIFI_ENABLED == 1"
  #else
    #define APP_MATTER_ENABLED 0
  #endif
#else
  #define APP_MATTER_ENABLED 0
#endif

#endif
