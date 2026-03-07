#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "cJSON.h"

// Max supported pulse sequences per bit definition
#define MAX_PULSES_PER_BIT 8

typedef struct {
    uint16_t high;
    uint16_t low;
} pulse_pair_t;

typedef struct {
    char name[16];
    uint16_t freq; // 433 or 868
    
    // Timing constraints (microsecond)
    uint16_t short_pulse;
    uint16_t long_pulse;
    uint16_t tolerance;
    
    // Bit patterns (S=Short, L=Long) - simplified representation
    // e.g. bit0 might be "SL" (Short High, Long Low)
    // or detailed timings:
    pulse_pair_t bit0[MAX_PULSES_PER_BIT];
    uint8_t bit0_len;
    
    pulse_pair_t bit1[MAX_PULSES_PER_BIT];
    uint8_t bit1_len;
    
    pulse_pair_t sync[MAX_PULSES_PER_BIT];
    uint8_t sync_len;
    
    uint16_t min_bits;
    uint16_t max_bits;
    
} rf_protocol_t;

void generic_decoder_init();
bool generic_decoder_load_from_json(const char* json_string);
void generic_decoder_process_pulse(uint16_t duration, uint8_t level);
