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
static uint32_t bridge_uptime_sec = 0;

static void matter_bridge_periodic_task(void* arg) {
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        bridge_uptime_sec++;
        if (bridge_uptime_sec % 60 == 0) {
            ESP_LOGI(TAG, "Matter Bridge Heartbeat: %d devices bridged. Uptime: %d min.", device_count, bridge_uptime_sec / 60);
        }
    }
}

void matter_bridge_init() {
    ESP_LOGI(TAG, "Initializing Matter Bridge Logic...");
    memset(device_table, 0, sizeof(device_table));
    
    // Initialize the underlying Matter stack (or simulation)
    matter_interface_init();

    xTaskCreate(matter_bridge_periodic_task, "matter_bridge_task", 2048, NULL, 5, NULL);
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
        
        char web_msg[64];
        snprintf(web_msg, sizeof(web_msg), "Matter %s -> %.1f", id, value);
        slowrf_add_web_event(web_msg);
    }
}

void matter_bridge_list_endpoints() {
    char out[128];
    int len = snprintf(out, sizeof(out), "Matter Bridge: Uptime %d s, Devices %d\r\n", bridge_uptime_sec, device_count);
    usb_serial_jtag_write_bytes(out, len, 0);

    for (int i = 0; i < device_count; i++) {
        len = snprintf(out, sizeof(out), "EP %d: ID=%s Type=%d\r\n", 
                           device_table[i].matter_ep_id, device_table[i].rf_id, device_table[i].type);
        usb_serial_jtag_write_bytes(out, len, 0);
    }
}

int matter_bridge_get_web_list(char* buf, int max_len) {
    int len = snprintf(buf, max_len, "<h3>Matter Bridge</h3><p>Devices: %d, Uptime: %d s</p><ul>", device_count, bridge_uptime_sec);
    for (int i = 0; i < device_count; i++) {
        len += snprintf(buf + len, max_len - len, "<li>EP %d: %s (Type: %d)</li>", 
                        device_table[i].matter_ep_id, device_table[i].rf_id, device_table[i].type);
        if (len > max_len - 50) break;
    }
    len += snprintf(buf + len, max_len - len, "</ul>");
    return len;
}
