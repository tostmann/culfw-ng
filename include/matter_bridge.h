#ifndef MATTER_BRIDGE_H
#define MATTER_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    DEVICE_TYPE_TEMPERATURE,
    DEVICE_TYPE_HUMIDITY,
    DEVICE_TYPE_SWITCH,
    DEVICE_TYPE_CONTACT
} matter_device_type_t;

typedef struct {
    char id[16];                // Funk-ID (z.B. FS20 HC+AD)
    matter_device_type_t type;  // Matter Gerätetyp
    float value;                // Letzter bekannter Wert
    uint16_t endpoint_id;       // Dynamische Matter Endpoint ID
} matter_endpoint_t;

void matter_bridge_init();
void matter_bridge_report_event(const char* id, matter_device_type_t type, float value);
void matter_bridge_list_endpoints();

#endif
