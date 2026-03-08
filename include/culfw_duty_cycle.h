#ifndef CULFW_DUTY_CYCLE_H
#define CULFW_DUTY_CYCLE_H

#include <stdint.h>
#include <stdbool.h>

void duty_cycle_init();
bool duty_cycle_add_tx(uint32_t duration_ms);
uint32_t duty_cycle_get_remaining();

#endif
