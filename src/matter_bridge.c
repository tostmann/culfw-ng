#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "matter_bridge.h"
#include "matter_wrapper.h"

static const char *TAG = "MATTER_BRIDGE";
#define MAX_ENDPOINTS 10
static matter_endpoint_t endpoints[MAX_ENDPOINTS];
static int endpoint_count = 0;

void matter_bridge_init() {
    ESP_LOGI(TAG, "Initializing Matter Bridge...");
    memset(endpoints, 0, sizeof(endpoints));
    // Here we would call esp_matter::start(app_event_cb);
    esp_matter_start(NULL);
}

// Internal function to create endpoint
static uint16_t create_matter_endpoint(const char* id, matter_device_type_t type) {
    uint16_t ep_id = 10 + endpoint_count; // Dummy EP ID generator

    // Here we would use the ESP-Matter Endpoint API
    if (type == DEVICE_TYPE_SWITCH) {
        // esp_matter::endpoint::on_off_light::create(node, &config, ENDPOINT_FLAG_NONE, NULL);
        ESP_LOGI(TAG, "Creating ON/OFF Light Endpoint: %d for ID: %s", ep_id, id);
    } else if (type == DEVICE_TYPE_TEMPERATURE) {
        // esp_matter::endpoint::temperature_sensor::create(node, &config, ENDPOINT_FLAG_NONE, NULL);
        ESP_LOGI(TAG, "Creating Temperature Sensor Endpoint: %d for ID: %s", ep_id, id);
    }

    return ep_id;
}

void matter_bridge_report_event(const char* id, matter_device_type_t type, float value) {
    // 1. Check if we already have this device
    int idx = -1;
    for (int i = 0; i < endpoint_count; i++) {
        if (strcmp(endpoints[i].id, id) == 0) {
            idx = i;
            break;
        }
    }

    // 2. If not found, add it
    if (idx == -1 && endpoint_count < MAX_ENDPOINTS) {
        idx = endpoint_count++;
        strncpy(endpoints[idx].id, id, 15);
        endpoints[idx].type = type;
        endpoints[idx].endpoint_id = create_matter_endpoint(id, type);
        ESP_LOGI(TAG, "New Matter Device registered: %s (Type %d) on Endpoint %d", 
                 id, type, endpoints[idx].endpoint_id);
    }

    // 3. Update Value (via Attribute API)
    if (idx != -1) {
        endpoints[idx].value = value;
        ESP_LOGI(TAG, "Matter Report [%s]: %.2f (Endpoint %d)", 
                 id, value, endpoints[idx].endpoint_id);
        
        // This is where we call esp_matter::attribute::update
        // e.g. esp_matter_attribute_update(endpoints[idx].endpoint_id, CLUSTER_ON_OFF, ATTRIBUTE_ON_OFF, &val);
    }
}
