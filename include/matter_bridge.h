#ifndef MATTER_BRIDGE_H
#define MATTER_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include "matter_interface.h"

typedef struct {
    char id[16];                // Funk-ID (z.B. FS20 HC+AD)
    matter_device_type_t type;  // Matter Gerätetyp
    float value;                // Letzter bekannter Wert
    uint16_t endpoint_id;       // Dynamische Matter Endpoint ID
} matter_endpoint_t;

void matter_bridge_init();
void matter_bridge_report_event(const char* id, const char* proto, matter_device_type_t type, float value);
void matter_bridge_list_endpoints();
int matter_bridge_get_web_list(char* buf, int max_len);

#endif
