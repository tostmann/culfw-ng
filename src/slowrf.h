#ifndef SLOWRF_H
#define SLOWRF_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define SLOWRF_MODE_CUL         0x21
#define SLOWRF_MODE_SIGNALDUINO 0x25

esp_err_t slowrf_init();
void slowrf_task(void *pvParameters);
void slowrf_set_reporting(bool enable);
void slowrf_set_debug(bool enable);
void slowrf_set_mode(uint8_t mode);
uint8_t slowrf_get_mode();

#endif
