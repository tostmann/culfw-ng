#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "matter_bridge.h"

static const char *TAG = "MATTER_BRIDGE";
#define MAX_ENDPOINTS 10
static matter_endpoint_t endpoints[MAX_ENDPOINTS];
static int endpoint_count = 0;

void matter_bridge_init() {
    ESP_LOGI(TAG, "Initializing Matter Bridge Foundation...");
    memset(endpoints, 0, sizeof(endpoints));
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
        endpoints[idx].endpoint_id = 10 + idx; // Just a dummy Matter Endpoint ID
        ESP_LOGI(TAG, "New Matter Device registered: %s (Type %d) on Endpoint %d", 
                 id, type, endpoints[idx].endpoint_id);
    }

    // 3. Update Value
    if (idx != -1) {
        endpoints[idx].value = value;
        ESP_LOGI(TAG, "Matter Report [%s]: %.2f (Endpoint %d)", 
                 id, value, endpoints[idx].endpoint_id);
        
        // TODO: Call esp-matter stack function here to notify controller (chip-tool)
    }
}
