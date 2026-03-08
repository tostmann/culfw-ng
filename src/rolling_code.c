#include "rolling_code.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "ROLLING_CODE";

uint16_t rolling_code_get_and_inc(const char* device_id) {
    nvs_handle_t my_handle;
    uint16_t code = 0;
    
    if (nvs_open("rolling", NVS_READWRITE, &my_handle) == ESP_OK) {
        if (nvs_get_u16(my_handle, device_id, &code) != ESP_OK) {
            code = 1; // Start with 1
        }
        
        uint16_t next_code = code + 1;
        nvs_set_u16(my_handle, device_id, next_code);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for rolling code");
    }
    
    return code;
}
