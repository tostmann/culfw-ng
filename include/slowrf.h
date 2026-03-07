#pragma once

#include "esp_err.h"
#include <stdbool.h>

#define SLOWRF_MODE_CUL         0x21
#define SLOWRF_MODE_SIGNALDUINO 0x25

/**
 * @brief Initialize SlowRF RX/TX Task
 */
esp_err_t slowrf_init();

/**
 * @brief Send raw sequence (CUL format)
 */
void slowrf_tx_sequence(const char* seq);

/**
 * @brief Enable/Disable Debug Output
 */
void slowrf_set_debug(bool enable);

/**
 * @brief Enable/Disable Reporting (RX)
 */
void slowrf_set_reporting(bool enable);

/**
 * @brief Set Operation Mode (X21 = CUL, X25 = SIGNALduino)
 */
void slowrf_set_mode(uint8_t mode);

/**
 * @brief Get Current Mode
 */
uint8_t slowrf_get_mode();
