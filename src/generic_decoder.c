#include "generic_decoder.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "GEN_DEC";

// Temporary storage for protocols
#define MAX_PROTOCOLS 10
static rf_protocol_t protocols[MAX_PROTOCOLS];
static int protocol_count = 0;

void generic_decoder_init() {
    protocol_count = 0;
    ESP_LOGI(TAG, "Generic decoder initialized. 0 protocols loaded.");
}

bool generic_decoder_load_from_json(const char* json_string) {
    ESP_LOGI(TAG, "Parsing JSON config...");
    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "Error before: %s", error_ptr);
        }
        return false;
    }

    const cJSON *proto_list = cJSON_GetObjectItemCaseSensitive(root, "protocols");
    if (!cJSON_IsArray(proto_list)) {
        ESP_LOGE(TAG, "No 'protocols' array found");
        cJSON_Delete(root);
        return false;
    }

    const cJSON *proto = NULL;
    cJSON_ArrayForEach(proto, proto_list) {
        if (protocol_count >= MAX_PROTOCOLS) break;
        
        cJSON *name = cJSON_GetObjectItemCaseSensitive(proto, "name");
        cJSON *freq = cJSON_GetObjectItemCaseSensitive(proto, "freq");

        if (cJSON_IsString(name) && (name->valuestring != NULL)) {
            rf_protocol_t *p = &protocols[protocol_count];
            strncpy(p->name, name->valuestring, sizeof(p->name)-1);
            p->freq = cJSON_IsNumber(freq) ? freq->valueint : 433;
            
            ESP_LOGI(TAG, "Loaded Protocol: %s (%d MHz)", p->name, p->freq);
            protocol_count++;
        }
    }

    cJSON_Delete(root);
    return true;
}

void generic_decoder_process_pulse(uint16_t duration, uint8_t level) {
    // Placeholder for future logic
}
