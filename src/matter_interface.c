#include "matter_interface.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "MATTER_IF";
static matter_command_cb_t cmd_cb = NULL;

// If the SDK is available, include it here
#ifdef CONFIG_ESP_MATTER_ENABLE
    #include <esp_matter.h>
    #include <esp_matter_console.h>
    #include <esp_matter_ota.h>
    
    using namespace esp_matter;
    using namespace esp_matter::attribute;
    using namespace esp_matter::endpoint;

    // SDK-specific static functions would go here
    static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg) {
        // Handle Matter events (commissioning, etc.)
    }
#endif

void matter_interface_init(void) {
#ifdef CONFIG_ESP_MATTER_ENABLE
    ESP_LOGI(TAG, "Starting ESP-Matter SDK...");
    esp_matter::start(app_event_cb);
#else
    ESP_LOGW(TAG, "Matter SDK NOT ENABLED. Running in simulation mode.");
#endif
}

uint16_t matter_interface_create_endpoint(const char* device_id, matter_device_type_t type) {
    uint16_t endpoint_id = 0xFFFF;

#ifdef CONFIG_ESP_MATTER_ENABLE
    node_t *node = node::get();
    endpoint_t *endpoint = nullptr;
    
    // Create actual Matter Endpoint based on type
    switch(type) {
        case DEVICE_TYPE_SWITCH:
            endpoint = on_off_light::create(node, nullptr, ENDPOINT_FLAG_NONE, nullptr);
            break;
        case DEVICE_TYPE_TEMP_SENSOR:
            endpoint = temperature_sensor::create(node, nullptr, ENDPOINT_FLAG_NONE, nullptr);
            break;
        // ... add more types
        default:
            ESP_LOGE(TAG, "Unknown device type: %d", type);
            return 0xFFFF;
    }

    if (endpoint) {
        endpoint_id = endpoint::get_id(endpoint);
        ESP_LOGI(TAG, "Created Matter Endpoint %d for device %s", endpoint_id, device_id);
    }
#else
    // Simulation: Generate pseudo-random ID based on string hash or counter
    static uint16_t counter = 10;
    endpoint_id = counter++;
    ESP_LOGI(TAG, "[SIMULATION] Created Endpoint %d for device %s (Type: %d)", endpoint_id, device_id, type);
#endif

    return endpoint_id;
}

void matter_interface_update_attribute(uint16_t endpoint_id, float value) {
#ifdef CONFIG_ESP_MATTER_ENABLE
    // Update actual Matter Attribute
    // This part requires mapping the type to the correct cluster/attribute ID
    // simplified example:
    esp_matter_attr_val_t val = esp_matter_bool(value > 0);
    attribute::update(endpoint_id, CLUSTER_ON_OFF_ID, ATTRIBUTE_ON_OFF_ID, &val);
#else
    ESP_LOGI(TAG, "[SIMULATION] Updated Endpoint %d to value %.2f", endpoint_id, value);
#endif
}

const char* matter_interface_get_status(void) {
#ifdef CONFIG_ESP_MATTER_ENABLE
    return "REAL";
#else
    return "SIMULATED";
#endif
}

void matter_interface_register_command_cb(matter_command_cb_t cb) {
    cmd_cb = cb;
}

// Internal: simulate receiving a command
void matter_interface_simulate_command(uint16_t endpoint_id, float value) {
    if (cmd_cb) {
        cmd_cb(endpoint_id, value);
    }
}
