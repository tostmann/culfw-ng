#ifndef SLOWRF_H
#define SLOWRF_H

#include <stdbool.h>

#include "esp_err.h"
esp_err_t slowrf_init();
void slowrf_task(void *pvParameters);
void slowrf_set_reporting(bool enable);
void slowrf_set_debug(bool enable);

#endif
