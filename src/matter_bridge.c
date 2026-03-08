#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "driver/usb_serial_jtag.h"
#include "matter_bridge.h"
#include "matter_interface.h"
#include "slowrf.h"
#include "cc1101.h"

static const char *TAG = "MATTER_BRIDGE";
#define MAX_ENDPOINTS 20

typedef struct {
    char rf_id[64];
    char proto_name[16];
    uint16_t matter_ep_id;
    matter_device_type_t type;
} bridged_device_t;

static bridged_device_t device_table[MAX_ENDPOINTS];
static int device_count = 0;
static uint32_t bridge_uptime_sec = 0;

// Callback for commands from Matter side
static void matter_bridge_command_cb(uint16_t endpoint_id, float value) {
    ESP_LOGI(TAG, "Command received from Matter: EP %d -> %.1f", endpoint_id, value);
    
    // Find device in our table
    int idx = -1;
    for (int i = 0; i < device_count; i++) {
        if (device_table[i].matter_ep_id == endpoint_id) {
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        ESP_LOGW(TAG, "No RF device found for EP %d", endpoint_id);
        return;
    }

    char* rf_id = device_table[idx].rf_id;
    char* proto = device_table[idx].proto_name;
    ESP_LOGI(TAG, "Translating Matter command to RF for device: %s (Proto: %s)", rf_id, proto);

    if (strcmp(proto, "FS20") == 0) {
        // ID format: FHHHHAA
        char hc[5], ad[3];
        strncpy(hc, rf_id + 1, 4); hc[4] = 0;
        strncpy(ad, rf_id + 5, 2); ad[2] = 0;
        const char* cmd = (value > 0) ? "11" : "00";
        cc1101_send_fs20(hc, ad, cmd);
    } 
    else if (strcmp(proto, "IT_V1") == 0) {
        // ID format: 00000000000X (12 chars, last one masked)
        char final_id[13];
        strncpy(final_id, rf_id, 12);
        final_id[12] = 0;
        if (final_id[11] == 'X') {
            final_id[11] = (value > 0) ? '0' : 'F'; // IT_V1 usually uses '0' for ON and 'F' for OFF or similar
        }
        ESP_LOGI(TAG, "IT_V1 TX: %s", final_id);
        cc1101_send_it_v1(final_id);
    }
    else if (strcmp(proto, "IT_V3") == 0 || strcmp(proto, "Nexa") == 0) {
        // ID is Nexa_HHHHHH or IT_V3_HHHHHH (26-bit ID in Hex)
        char* hex_start = strchr(rf_id, '_');
        if (hex_start) {
            hex_start++; 
            char bits[33];
            memset(bits, '0', 32);
            bits[32] = 0;
            
            uint32_t val = strtoul(hex_start, NULL, 16);
            // Reconstruct 26-bit ID (bits 0..25)
            for(int i=0; i<26; i++) {
                bits[25-i] = ((val >> i) & 1) ? '1' : '0';
            }
            
            // Bit 26: Group (0)
            bits[26] = '0'; 
            // Bit 27: On/Off (On=1, Off=0)
            bits[27] = (value > 0) ? '1' : '0';
            
            // Bits 28..31: Unit (0000)
            ESP_LOGI(TAG, "Nexa/IT_V3 TX Bits: %s", bits);
            cc1101_send_it_v3(bits);
        }
    }
    else if (strcmp(proto, "Oregon") == 0) {
        cc1101_send_oregon(rf_id);
    }
    else {
        ESP_LOGW(TAG, "TX not supported for protocol: %s", proto);
    }
}

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

    // Register our callback to receive commands from Matter
    matter_interface_register_command_cb(matter_bridge_command_cb);

    xTaskCreate(matter_bridge_periodic_task, "matter_bridge_task", 2048, NULL, 5, NULL);
}

void matter_bridge_report_event(const char* id, const char* proto, matter_device_type_t type, float value) {
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
        strncpy(device_table[idx].proto_name, proto, sizeof(device_table[idx].proto_name)-1);
        device_table[idx].type = type;
        
        // Call the interface to create the actual Matter endpoint
        device_table[idx].matter_ep_id = matter_interface_create_endpoint(id, type);
        
        ESP_LOGI(TAG, "Registered new device: %s -> EP %d (Proto: %s)", id, device_table[idx].matter_ep_id, proto);
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
        len = snprintf(out, sizeof(out), "EP %d: ID=%s Type=%d Proto=%s\r\n", 
                           device_table[i].matter_ep_id, device_table[i].rf_id, device_table[i].type, device_table[i].proto_name);
        usb_serial_jtag_write_bytes(out, len, 0);
    }
}

int matter_bridge_get_web_list(char* buf, int max_len) {
    int len = snprintf(buf, max_len, "<h3>Matter Bridge</h3><p>Devices: %d, Uptime: %d s</p><ul>", device_count, bridge_uptime_sec);
    for (int i = 0; i < device_count; i++) {
        len += snprintf(buf + len, max_len - len, "<li>EP %d: %s (Type: %d, Proto: %s)</li>", 
                        device_table[i].matter_ep_id, device_table[i].rf_id, device_table[i].type, device_table[i].proto_name);
        if (len > max_len - 50) break;
    }
    len += snprintf(buf + len, max_len - len, "</ul>");
    return len;
}
