#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "driver/usb_serial_jtag.h"
#include "matter_bridge.h"
#include "matter_interface.h"
#include "slowrf.h"

static const char *TAG = "MATTER_BRIDGE";
#define MAX_ENDPOINTS 20

typedef struct {
    char rf_id[16];
    uint16_t matter_ep_id;
    matter_device_type_t type;
} bridged_device_t;

static bridged_device_t device_table[MAX_ENDPOINTS];
static int device_count = 0;

void matter_bridge_init() {
    ESP_LOGI(TAG, "Initializing Matter Bridge Logic...");
    memset(device_table, 0, sizeof(device_table));
    
    // Initialize the underlying Matter stack (or simulation)
    matter_interface_init();
}

void matter_bridge_report_event(const char* id, matter_device_type_t type, float value) {
    // 1. Lookup Device in our local table
    int idx = -1;
    for (int i = 0; i < device_count; i++) {
        if (strcmp(device_table[i].rf_id, id) == 0) {
            idx = i;
            break;
        }
    }

    // 2. If not found, register it
    if (idx == -1) {
        if (device_count >= MAX_ENDPOINTS) {
            ESP_LOGW(TAG, "Device table full! Cannot add %s", id);
            return;
        }
        
        idx = device_count++;
        strncpy(device_table[idx].rf_id, id, sizeof(device_table[idx].rf_id)-1);
        device_table[idx].type = type;
        
        // Call the interface to create the actual Matter endpoint
        device_table[idx].matter_ep_id = matter_interface_create_endpoint(id, type);
        
        ESP_LOGI(TAG, "Registered new device: %s -> EP %d", id, device_table[idx].matter_ep_id);
    }

    // 3. Update the value
    if (idx != -1 && device_table[idx].matter_ep_id != 0xFFFF) {
        matter_interface_update_attribute(device_table[idx].matter_ep_id, value);
    }
}

void matter_bridge_list_endpoints() {
    char out[128];
    for (int i = 0; i < device_count; i++) {
        int len = snprintf(out, sizeof(out), "EP %d: ID=%s Type=%d\r\n", 
                           device_table[i].matter_ep_id, device_table[i].rf_id, device_table[i].type);
        usb_serial_jtag_write_bytes(out, len, 0);
    }
}
