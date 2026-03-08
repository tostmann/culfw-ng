#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    DEVICE_TYPE_SWITCH = 0,
    DEVICE_TYPE_TEMP_SENSOR,
    DEVICE_TYPE_CONTACT_SENSOR,
    DEVICE_TYPE_DIMMER,
    DEVICE_TYPE_OUTLET,
    DEVICE_TYPE_LIGHT,
    DEVICE_TYPE_COVER
} matter_device_type_t;

typedef void (*matter_command_cb_t)(uint16_t endpoint_id, float value);

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

/**
 * @brief Get status string of Matter implementation
 * @return "SIMULATED" or "REAL"
 */
const char* matter_interface_get_status(void);

/**
 * @brief Register a callback for incoming commands (e.g. Turn On/Off)
 */
void matter_interface_register_command_cb(matter_command_cb_t cb);

/**
 * @brief Internal/Test: simulate receiving a command
 */
void matter_interface_simulate_command(uint16_t endpoint_id, float value);

#ifdef __cplusplus
}
#endif
