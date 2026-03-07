#include "config_loader.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "generic_decoder.h"
#include <stdio.h>
#include <sys/unistd.h>
#include <sys/stat.h>

static const char *TAG = "CONFIG";

bool config_loader_init() {
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/data",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return false;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    
    return true;
}

bool config_loader_load_protocols() {
    FILE* f = fopen("/data/protocols.json", "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "Failed to open protocols.json, using default fallback.");
        // Fallback: Use minimal JSON or return false
        // For development, we return a hardcoded simple string if file missing
        const char* fallback_json = "{\"protocols\":[{\"name\":\"Fallback_Test\",\"freq\":433}]}";
        return generic_decoder_load_from_json(fallback_json);
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        ESP_LOGE(TAG, "File empty");
        fclose(f);
        return false;
    }

    char* buffer = malloc(size + 1);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Out of memory");
        fclose(f);
        return false;
    }

    fread(buffer, 1, size, f);
    buffer[size] = 0; // Null terminate
    fclose(f);

    ESP_LOGI(TAG, "Loaded JSON (%ld bytes)", size);
    bool result = generic_decoder_load_from_json(buffer);
    
    free(buffer);
    return result;
}
