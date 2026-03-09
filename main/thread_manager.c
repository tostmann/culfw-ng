#include "thread_manager.h"
#include "esp_log.h"
#include "esp_openthread.h"
#include "esp_openthread_types.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_vfs_eventfd.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "openthread/instance.h"
#include "openthread/thread.h"

static const char *TAG = "THREAD_MGR";

#ifdef CONFIG_OPENTHREAD_ENABLED

#define CUL_OPENTHREAD_DEFAULT_RADIO_CONFIG() \
    {                                         \
        .radio_mode = RADIO_MODE_NATIVE,      \
    }

#define CUL_OPENTHREAD_DEFAULT_HOST_CONFIG()                \
    {                                                       \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE,  \
    }

#define CUL_OPENTHREAD_DEFAULT_PORT_CONFIG()    \
    {                                           \
        .storage_partition_name = "nvs",        \
        .netif_queue_size = 10,                 \
        .task_queue_size = 10,                  \
    }

static void ot_task_worker(void *aContext) {
    esp_openthread_platform_config_t config = {
        .radio_config = CUL_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = CUL_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = CUL_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };

    // Initialize the OpenThread stack
    ESP_ERROR_CHECK(esp_openthread_init(&config));

    // Initialize the esp-netif glue
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_OPENTHREAD();
    esp_netif_t *openthread_netif = esp_netif_new(&cfg);
    assert(openthread_netif);
    
    // Create the eventfd for VFS
    esp_vfs_eventfd_config_t eventfd_config = ESP_VFS_EVENTD_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));

    // Join the openthread and netif
    esp_openthread_netif_glue_init(&config);
    
    // The main loop for OpenThread
    esp_openthread_launch_main_loop();

    // Clean up
    esp_openthread_netif_glue_deinit();
    esp_netif_destroy(openthread_netif);
    esp_openthread_deinit();
    vTaskDelete(NULL);
}

esp_err_t thread_manager_init(void) {
    ESP_LOGI(TAG, "Initializing Thread Stack...");
    xTaskCreate(ot_task_worker, "ot_worker", 8192, NULL, 5, NULL);
    return ESP_OK;
}
#else
esp_err_t thread_manager_init(void) {
    ESP_LOGW(TAG, "OpenThread is NOT enabled in sdkconfig. Skipping.");
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

void thread_manager_start(void) {
    ESP_LOGI(TAG, "Starting Thread...");
}
