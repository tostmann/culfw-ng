#include "generic_decoder.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "GEN_DEC";

#define MAX_PROTOCOLS 10
#define MAX_PULSES 8

typedef struct {
    uint16_t high;
    uint16_t low;
} pulse_pair_us_t;

typedef struct {
    char name[16];
    uint16_t freq; 
    
    uint16_t short_us;
    uint16_t long_us;
    uint16_t tolerance_us;
    
    pulse_pair_us_t bit0[MAX_PULSES];
    uint8_t bit0_len;
    
    pulse_pair_us_t bit1[MAX_PULSES];
    uint8_t bit1_len;
    
    pulse_pair_us_t sync[MAX_PULSES];
    uint8_t sync_len;
    
    uint16_t min_bits;
    uint16_t max_bits;
} rf_proto_internal_t;

typedef enum {
    STATE_WAIT_SYNC,
    STATE_READ_BITS
} dec_state_e;

typedef struct {
    dec_state_e state;
    uint8_t current_pulse_idx; // in current symbol (bit0/1/sync)
    uint8_t sync_pulse_idx;
    uint16_t bit_buffer_len;
    uint64_t bit_buffer;
    uint32_t last_sync_ts;
} proto_state_t;

static rf_proto_internal_t protocols[MAX_PROTOCOLS];
static proto_state_t states[MAX_PROTOCOLS];
static int protocol_count = 0;

void generic_decoder_init() {
    protocol_count = 0;
    memset(states, 0, sizeof(states));
    ESP_LOGI(TAG, "Generic decoder initialized.");
}

static void parse_pulse_def(cJSON *def, pulse_pair_us_t *out, uint8_t *len, uint16_t base_us) {
    if (!cJSON_IsArray(def)) return;
    *len = 0;
    cJSON *pair;
    cJSON_ArrayForEach(pair, def) {
        if (*len >= MAX_PULSES) break;
        cJSON *h = cJSON_GetObjectItem(pair, "h");
        cJSON *l = cJSON_GetObjectItem(pair, "l");
        if (h && l) {
            out[*len].high = h->valueint * base_us;
            out[*len].low = l->valueint * base_us;
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

        ESP_LOGI(TAG, "Loaded: %s (Short: %d us)", p->name, p->short_us);
        protocol_count++;
    }

    cJSON_Delete(root);
    return true;
}

static bool match_pulse(uint16_t duration, uint16_t target, uint16_t tol) {
    if (duration > target - tol && duration < target + tol) return true;
    return false;
}

// Simplified Logic: Just detect Sync for now
void generic_decoder_process_pulse(uint16_t duration, uint8_t level) {
    for (int i=0; i<protocol_count; i++) {
        rf_proto_internal_t *p = &protocols[i];
        // proto_state_t *s = &states[i];
        
        // Simple Sync Check (Level must match sync start)
        // Sync is usually High then Low.
        // We only check duration match on Short/Long for now to see if basics work
        
        if (match_pulse(duration, p->short_us, p->tolerance_us)) {
            // ESP_LOGI(TAG, "Short Pulse match for %s", p->name);
        }
    }
}
