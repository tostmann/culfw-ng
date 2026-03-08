#include "config_loader.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "generic_decoder.h"
#include "esp_mac.h"
#include <stdio.h>
#include <sys/unistd.h>
#include <sys/stat.h>

static const char *TAG = "CONFIG";

// IP Protection: Hardware binding
static bool config_loader_check_auth() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    ESP_LOGI(TAG, "Auth check for: %02X:%02X:%02X:%02X:%02X:%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
             
    // In a production environment, this would check against a signed list 
    // or use Secure Boot / Flash Encryption to ensure the check itself is not tampered with.
    // For now, we authorize all units but log the check.
    return true; 
}

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

static void config_loader_xor_decrypt(char* data, size_t len) {
    const char key[] = "CULFW-NG-IP-PROTECT-2024";
    size_t key_len = strlen(key);
    for (size_t i = 0; i < len; i++) {
        data[i] ^= key[i % key_len];
    }
}

bool config_loader_load_protocols() {
    // Auth check disabled for easy debugging as requested
    /*
    if (!config_loader_check_auth()) {
        ESP_LOGE(TAG, "Authorization failed! IP protection active.");
        return false;
    }
    */

    // Prefer plain JSON during debugging
    bool encrypted = false;
    FILE* f = fopen("/data/protocols.json", "r");
    if (f) {
        ESP_LOGI(TAG, "Found plain protocol database.");
    } else {
        f = fopen("/data/protocols.enc", "rb");
        if (f) {
            encrypted = true;
            ESP_LOGI(TAG, "Found encrypted protocol database.");
        }
    }

    if (f == NULL) {
        ESP_LOGW(TAG, "Failed to open protocols, using default fallback.");
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
    buffer[size] = 0; 
    fclose(f);

    if (encrypted) {
        config_loader_xor_decrypt(buffer, size);
    }

    ESP_LOGI(TAG, "Processing %s protocol database (%ld bytes)", encrypted ? "encrypted" : "plain", size);
    bool result = generic_decoder_load_from_json(buffer);
    
    free(buffer);
    return result;
}
