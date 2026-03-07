#pragma once

#include <stdbool.h>

/**
 * @brief Initialize SPIFFS and load configuration
 * 
 * @return true if successful
 */
bool config_loader_init();

/**
 * @brief Load protocols from SPIFFS
 * 
 * @return true if protocols loaded
 */
bool config_loader_load_protocols();
