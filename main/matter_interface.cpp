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

    static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data) {
        if (type == attribute::PRE_UPDATE) {
            ESP_LOGI(TAG, "Attribute Update: EP %d Cluster %lX Attr %lX Val %d", endpoint_id, cluster_id, attribute_id, val->val.b);
            if (cluster_id == chip::app::Clusters::OnOff::Id && attribute_id == chip::app::Clusters::OnOff::Attributes::OnOff::Id) {
                 matter_interface_simulate_command(endpoint_id, val->val.b ? 1.0f : 0.0f);
            }
        }
        return ESP_OK;
    }

    static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id, uint8_t effect_variant, void *priv_data) {
        return ESP_OK;
    }
#endif

void matter_interface_init(void) {
#ifdef CONFIG_ESP_MATTER_ENABLE
    ESP_LOGI(TAG, "Initializing ESP-Matter Node...");
    
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    
    if (node) {
        endpoint::aggregator::config_t aggregator_config;
        endpoint::aggregator::create(node, &aggregator_config, ENDPOINT_FLAG_NONE, NULL);
        ESP_LOGI(TAG, "Node and Aggregator created.");
    } else {
        ESP_LOGE(TAG, "Failed to create Node!");
    }

    ESP_LOGI(TAG, "Starting ESP-Matter SDK...");
    esp_matter::start(app_event_cb);

    // Manual pairing info
    // (Headers are actually already included via esp_matter.h or need to be at top)
    ESP_LOGW("MATTER_SETUP", "Manual Code: 34905722491"); // Default for 3840/20202021

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
        case DEVICE_TYPE_OUTLET:
            endpoint = on_off_plugin_unit::create(node, nullptr, ENDPOINT_FLAG_NONE, nullptr);
            break;
        case DEVICE_TYPE_COVER:
            endpoint = window_covering_device::create(node, nullptr, ENDPOINT_FLAG_NONE, nullptr);
            break;
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
    attribute::update(endpoint_id, chip::app::Clusters::OnOff::Id, chip::app::Clusters::OnOff::Attributes::OnOff::Id, &val);
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
