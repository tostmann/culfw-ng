#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize generic decoder engine
 */
void generic_decoder_init();

/**
 * @brief Load protocol definitions from JSON string
 * 
 * @param json_string Null-terminated JSON content
 * @return true on success
 */
bool generic_decoder_load_from_json(const char* json_string);

/**
 * @brief Process a single RF pulse
 * 
 * @param duration Duration in microseconds
 * @param level Logic level (1=High, 0=Low)
 */
void generic_decoder_process_pulse(uint16_t duration, uint8_t level);
