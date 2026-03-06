#ifndef SLOWRF_H
#define SLOWRF_H

#include "esp_err.h"
#include <stdbool.h>

esp_err_t slowrf_init();
void slowrf_set_debug(bool enable);

#endif
