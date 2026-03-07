#pragma once

#include <stdint.h>
#include <stdbool.h>

// Dummy defines to make the code compile without full ESP-Matter SDK
#ifndef CONFIG_ESP_MATTER_ENABLE
    typedef void* esp_matter_node_t;
    typedef void* esp_matter_endpoint_t;
    typedef void* esp_matter_cluster_t;
    typedef void* esp_matter_attribute_t;
    
    // Fake functions
    static inline void esp_matter_start(void* handler) {}
    static inline esp_matter_endpoint_t esp_matter_endpoint_create(esp_matter_node_t node, uint16_t endpoint_id, void* type, uint32_t flags) { return (void*)1; }
    static inline void esp_matter_attribute_update(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, void* val) {}
#else
    #include <esp_matter.h>
    #include <esp_matter_console.h>
    #include <esp_matter_ota.h>
#endif
