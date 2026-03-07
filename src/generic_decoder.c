#include "generic_decoder.h"
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
} rf_proto_internal_t;

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
    protocol_count = 0;
    memset(states, 0, sizeof(states));
    ESP_LOGI(TAG, "Generic decoder initialized.");
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
            
            // Check Bit 0
            if (possible0) {
                if (s->bit0_match_idx < p->bit0_len) {
                    pulse_def_t *tgt0 = &p->bit0[s->bit0_match_idx];
                    if (level != tgt0->level || !match_pulse(duration, tgt0->duration, p->tolerance_us)) {
                        s->match_bit0 = false;
                    } else {
                        s->bit0_match_idx++;
                    }
                } else {
                    // Index overflow (should have been caught)
                    s->match_bit0 = false; 
                }
            }
            
            // Check Bit 1
            if (possible1) {
                if (s->bit1_match_idx < p->bit1_len) {
                    pulse_def_t *tgt1 = &p->bit1[s->bit1_match_idx];
                    if (level != tgt1->level || !match_pulse(duration, tgt1->duration, p->tolerance_us)) {
                        s->match_bit1 = false;
                    } else {
                        s->bit1_match_idx++;
                    }
                } else {
                     s->match_bit1 = false;
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
                    // Ambiguous! (e.g. bit0 is prefix of bit1)
                    // We need more pulses to decide.
                    // Assuming no prefix codes for now.
                    // Prefer Bit 1? Or Error?
                    // For now: reset.
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
                    char msg[64];
                    int len = snprintf(msg, sizeof(msg), "GEN:%s:%llX\r\n", p->name, s->bit_buffer);
                    usb_serial_jtag_write_bytes(msg, len, 0);
                    ESP_LOGI(TAG, "DECODED %s: %llX", p->name, s->bit_buffer);
                    
                    reset_state(s);
                }
            }
        }
    }
}
