#include "generic_decoder.h"
#include "slowrf.h"
#include "cc1101.h"
#include "matter_bridge.h"
#include "esp_log.h"
#include "cJSON.h"
#include "driver/usb_serial_jtag.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "GEN_DEC";

#define MAX_PROTOCOLS 10
#define MAX_SEQ_LEN 16  // Max pulses per symbol (sync/bit)

typedef struct {
    uint16_t duration;
    uint8_t level; // 1=High, 0=Low
} pulse_def_t;

typedef struct {
    char name[16];
    uint16_t freq; 
    
    uint16_t short_us;
    uint16_t long_us;
    uint16_t tolerance_us;
    
    pulse_def_t sync[MAX_SEQ_LEN];
    uint8_t sync_len;
    
    pulse_def_t bit0[MAX_SEQ_LEN];
    uint8_t bit0_len;
    
    pulse_def_t bit1[MAX_SEQ_LEN];
    uint8_t bit1_len;
    
    uint16_t min_bits;
    uint16_t max_bits;
    int matter_type; // 0=Switch, 1=Sensor
    uint32_t count_decoded;
} rf_proto_internal_t;

static void generic_decoder_output_packet(rf_proto_internal_t *p, uint64_t data, uint8_t rssi);

typedef enum {
    STATE_WAIT_SYNC,
    STATE_READ_BITS
} dec_state_e;

typedef struct {
    dec_state_e state;
    uint8_t seq_idx; // Index in current sequence (sync or bit)
    uint16_t bit_cnt;
    uint64_t bit_buffer; // Simple buffer for up to 64 bits
    // For determining bit0 vs bit1, we need to track both possibilities
    uint8_t bit0_match_idx;
    uint8_t bit1_match_idx;
    bool match_bit0;
    bool match_bit1;
} proto_state_t;

static rf_proto_internal_t protocols[MAX_PROTOCOLS];
static proto_state_t states[MAX_PROTOCOLS];
static int protocol_count = 0;

void generic_decoder_init() {
    // We do NOT reset protocol_count here, as protocols might be loaded before the task starts.
    // Instead, we only reset the states.
    memset(states, 0, sizeof(states));
    ESP_LOGI(TAG, "Generic decoder state initialized.");
}

static void generic_decoder_output_packet(rf_proto_internal_t *p, uint64_t data, uint8_t rssi) {
    slowrf_mark_matched();
    uint8_t mode = slowrf_get_mode();
    char msg[128];
    int len = 0;

    if (mode == SLOWRF_MODE_SIGNALDUINO) {
        // MS;P0=short;P1=long;D=bits;CP=0;SP=sync;
        len = snprintf(msg, sizeof(msg), "MS;P0=%d;P1=%d;D=%llX;RSS=%d;\r\n", 
                       p->short_us, p->long_us, data, rssi);
    } else {
        // CUL mode - we use our generic prefix
        len = snprintf(msg, sizeof(msg), "G%s%llX%02X\r\n", p->name, data, rssi);
    }

    if (len > 0) {
        usb_serial_jtag_write_bytes(msg, len, portMAX_DELAY);
    }

    // Web Event
    char web_msg[64];
    snprintf(web_msg, sizeof(web_msg), "G:%s %llX (%d)", p->name, data, rssi);
    slowrf_add_web_event(web_msg);
    
    // Also notify Matter Bridge
    char id_str[64];
    // Heuristic: Use name + upper bits as ID, last 8 bits as value
    snprintf(id_str, sizeof(id_str), "%s_%llX", p->name, (data >> 8));
    matter_bridge_report_event(id_str, p->matter_type == 1 ? DEVICE_TYPE_TEMP_SENSOR : DEVICE_TYPE_SWITCH, (float)(data & 0xFF));
}

// Helper to flatten JSON pairs into sequence
static void parse_pulse_def(cJSON *def, pulse_def_t *out, uint8_t *len, uint16_t base_us) {
    if (!cJSON_IsArray(def)) return;
    *len = 0;
    cJSON *pair;
    cJSON_ArrayForEach(pair, def) {
        if (*len >= MAX_SEQ_LEN - 1) break;
        cJSON *h = cJSON_GetObjectItem(pair, "h");
        cJSON *l = cJSON_GetObjectItem(pair, "l");
        
        // Add High Pulse
        if (h) {
            out[*len].duration = h->valueint * base_us;
            out[*len].level = 1;
            (*len)++;
        }
        // Add Low Pulse
        if (l) {
            out[*len].duration = l->valueint * base_us;
            out[*len].level = 0;
            (*len)++;
        }
    }
}

bool generic_decoder_load_from_json(const char* json_string) {
    ESP_LOGI(TAG, "Parsing JSON config...");
    cJSON *root = cJSON_Parse(json_string);
    if (!root) {
        ESP_LOGE(TAG, "JSON Parse Error");
        return false;
    }

    cJSON *proto_list = cJSON_GetObjectItem(root, "protocols");
    cJSON *proto;
    
    protocol_count = 0;

    cJSON_ArrayForEach(proto, proto_list) {
        if (protocol_count >= MAX_PROTOCOLS) break;
        
        rf_proto_internal_t *p = &protocols[protocol_count];
        memset(p, 0, sizeof(rf_proto_internal_t));

        cJSON *name = cJSON_GetObjectItem(proto, "name");
        if (name) strncpy(p->name, name->valuestring, 15);

        cJSON *freq = cJSON_GetObjectItem(proto, "freq");
        p->freq = freq ? freq->valueint : 433;

        cJSON *type = cJSON_GetObjectItem(proto, "type");
        if (type && type->valuestring && strcmp(type->valuestring, "sensor") == 0) {
            p->matter_type = 1;
        } else {
            p->matter_type = 0; // Default switch
        }

        cJSON *timing = cJSON_GetObjectItem(proto, "timing");
        if (timing) {
            p->short_us = cJSON_GetObjectItem(timing, "short")->valueint;
            p->long_us = cJSON_GetObjectItem(timing, "long")->valueint;
            p->tolerance_us = cJSON_GetObjectItem(timing, "tolerance")->valueint;
        }

        cJSON *defs = cJSON_GetObjectItem(proto, "definitions");
        if (defs) {
            parse_pulse_def(cJSON_GetObjectItem(defs, "bit0"), p->bit0, &p->bit0_len, p->short_us);
            parse_pulse_def(cJSON_GetObjectItem(defs, "bit1"), p->bit1, &p->bit1_len, p->short_us);
            parse_pulse_def(cJSON_GetObjectItem(defs, "sync"), p->sync, &p->sync_len, p->short_us);
        }
        
        cJSON *len = cJSON_GetObjectItem(proto, "len");
        if (len) {
            p->min_bits = cJSON_GetObjectItem(len, "min")->valueint;
            p->max_bits = cJSON_GetObjectItem(len, "max")->valueint;
        }

        ESP_LOGI(TAG, "Loaded: %s (Short: %d us, SyncLen: %d)", p->name, p->short_us, p->sync_len);
        protocol_count++;
    }

    cJSON_Delete(root);
    return true;
}

static bool match_pulse(uint16_t duration, uint16_t target, uint16_t tol) {
    // If target is 0, we skip check (should not happen in valid defs)
    if (target == 0) return true; 
    return (duration > target - tol && duration < target + tol);
}

static void reset_state(proto_state_t *s) {
    s->state = STATE_WAIT_SYNC;
    s->seq_idx = 0;
    s->bit_cnt = 0;
    s->bit_buffer = 0;
    s->match_bit0 = true; 
    s->match_bit1 = true;
    s->bit0_match_idx = 0;
    s->bit1_match_idx = 0;
}

void generic_decoder_process_pulse(uint16_t duration, uint8_t level) {
    // ESP_LOGD(TAG, "P:%d L:%d", duration, level);
    for (int i=0; i<protocol_count; i++) {
        rf_proto_internal_t *p = &protocols[i];
        proto_state_t *s = &states[i];
        
        // TODO: Frequency Check via global variable or passed param
        // For now, assume correct frequency

        if (s->state == STATE_WAIT_SYNC) {
            // Check Sync Sequence
            if (s->seq_idx < p->sync_len) {
                pulse_def_t *target = &p->sync[s->seq_idx];
                
                // Level Match?
                if (level == target->level) {
                    // Duration Match?
                    if (match_pulse(duration, target->duration, p->tolerance_us)) {
                        s->seq_idx++;
                        if (s->seq_idx >= p->sync_len) {
                            // Sync Complete!
                            ESP_LOGI(TAG, "Sync complete for %s", p->name);
                            s->state = STATE_READ_BITS;
                            s->bit_cnt = 0;
                            s->bit_buffer = 0;
                            
                            // Prepare for first bit
                            s->bit0_match_idx = 0;
                            s->bit1_match_idx = 0;
                            s->match_bit0 = true;
                            s->match_bit1 = true;
                        }
                    } else {
                        // Duration Mismatch -> Reset
                        // But wait: Maybe this pulse is the START of a new sync?
                        // If index was > 0, we reset to 0.
                        // If index was 0, it just didn't match.
                        s->seq_idx = 0;
                        
                        // Retry matching first pulse of sync immediately?
                        // Yes, if level matches first sync pulse.
                        if (level == p->sync[0].level && match_pulse(duration, p->sync[0].duration, p->tolerance_us)) {
                             s->seq_idx = 1; 
                        }
                    }
                } else {
                    // Wrong level -> Reset
                    s->seq_idx = 0;
                }
            }
        } else if (s->state == STATE_READ_BITS) {
            // We are reading bits. A bit can be bit0 or bit1 sequence.
            // We track both possibilities until one fails.
            
            bool possible0 = s->match_bit0;
            bool possible1 = s->match_bit1;
            // ESP_LOGI(TAG, "BitMatch: P:%d L:%d Pos0:%d Pos1:%d", duration, level, s->bit0_match_idx, s->bit1_match_idx);
            
            // Check Bit 0
            if (possible0) {
                pulse_def_t *tgt0 = &p->bit0[s->bit0_match_idx];
                if (level != tgt0->level || !match_pulse(duration, tgt0->duration, p->tolerance_us)) {
                    s->match_bit0 = false;
                    // ESP_LOGI(TAG, "%s: Bit0 mismatch at idx %d (L:%d vs %d, D:%d vs %d)", p->name, s->bit0_match_idx, level, tgt0->level, duration, tgt0->duration);
                } else {
                    s->bit0_match_idx++;
                }
            }
            
            // Check Bit 1
            if (possible1) {
                pulse_def_t *tgt1 = &p->bit1[s->bit1_match_idx];
                if (level != tgt1->level || !match_pulse(duration, tgt1->duration, p->tolerance_us)) {
                    s->match_bit1 = false;
                    // ESP_LOGI(TAG, "%s: Bit1 mismatch at idx %d (L:%d vs %d, D:%d vs %d)", p->name, s->bit1_match_idx, level, tgt1->level, duration, tgt1->duration);
                } else {
                    s->bit1_match_idx++;
                }
            }
            
            // Determine Outcome
            if (!s->match_bit0 && !s->match_bit1) {
                // Both failed -> decoding error -> reset
                reset_state(s);
            } else {
                // Check if any bit sequence completed
                bool bit0_complete = (s->match_bit0 && s->bit0_match_idx >= p->bit0_len);
                bool bit1_complete = (s->match_bit1 && s->bit1_match_idx >= p->bit1_len);
                
                if (bit0_complete && bit1_complete) {
                    // Ambiguous! 
                    ESP_LOGW(TAG, "%s: Bit ambiguity (0 & 1 complete)", p->name);
                    reset_state(s);
                } else if (bit0_complete) {
                    // Found Bit 0
                    s->bit_buffer = (s->bit_buffer << 1) | 0;
                    s->bit_cnt++;
                    
                    // Reset bit matchers for next bit
                    s->bit0_match_idx = 0; s->bit1_match_idx = 0;
                    s->match_bit0 = true; s->match_bit1 = true;
                    
                } else if (bit1_complete) {
                    // Found Bit 1
                    s->bit_buffer = (s->bit_buffer << 1) | 1;
                    s->bit_cnt++;
                    
                    // Reset bit matchers
                    s->bit0_match_idx = 0; s->bit1_match_idx = 0;
                    s->match_bit0 = true; s->match_bit1 = true;
                }
                
                // Check Packet Complete
                if (s->bit_cnt >= p->max_bits) {
                    // Success!
                    p->count_decoded++;
                    generic_decoder_output_packet(p, s->bit_buffer, cc1101_read_rssi());
                    ESP_LOGI(TAG, "DECODED %s: %llX", p->name, s->bit_buffer);
                    reset_state(s);
                }
            }
        }
    }
}

void generic_decoder_list_protocols() {
    char out[128];
    for (int i = 0; i < protocol_count; i++) {
        int len = snprintf(out, sizeof(out), "GP %d: Name=%s Freq=%d Bits=%d-%d Decoded=%lu\r\n", 
                           i, protocols[i].name, protocols[i].freq, protocols[i].min_bits, protocols[i].max_bits, protocols[i].count_decoded);
        usb_serial_jtag_write_bytes(out, len, 0);
    }
}

int generic_decoder_get_web_list(char* buf, int max_len) {
    int len = snprintf(buf, max_len, "<h3>Protocols</h3><ul>");
    for (int i = 0; i < protocol_count; i++) {
        len += snprintf(buf + len, max_len - len, "<li>%s (%d MHz, %d-%d bits): %lu Decoded</li>", 
                        protocols[i].name, protocols[i].freq, protocols[i].min_bits, protocols[i].max_bits, protocols[i].count_decoded);
        if (len > max_len - 100) break;
    }
    len += snprintf(buf + len, max_len - len, "</ul>");
    return len;
}
