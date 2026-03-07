#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    MATTER_DEVICE_SWITCH = 0,
    MATTER_DEVICE_TEMP_SENSOR,
    MATTER_DEVICE_CONTACT_SENSOR,
    MATTER_DEVICE_DIMMER
} matter_device_type_t;

/**
 * @brief Initialize the Matter Stack/Bridge
 */
void matter_interface_init(void);

/**
 * @brief Register a new dynamic endpoint
 * @param device_id Unique string ID from RF packet (e.g. "F123401")
 * @param type Device type (Switch, Temp, etc.)
 * @return Internal Endpoint ID or 0xFFFF on failure
 */
uint16_t matter_interface_create_endpoint(const char* device_id, matter_device_type_t type);

/**
 * @brief Update an attribute on an endpoint
 * @param endpoint_id The ID returned by create_endpoint
 * @param value The new value (float for generic usage, cast internally)
 */
void matter_interface_update_attribute(uint16_t endpoint_id, float value);
