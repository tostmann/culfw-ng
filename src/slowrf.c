#include "slowrf.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "SLOWRF";
static QueueHandle_t pulse_queue;
static int64_t last_time = 0;

static uint32_t isr_count = 0;
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    int64_t current_time = esp_timer_get_time();
    int64_t diff = current_time - last_time;
    last_time = current_time;
    
    isr_count++;
    if (diff > 100) { // Ignore extremely short glitches
        xQueueSendFromISR(pulse_queue, &diff, NULL);
    }
}

void slowrf_task(void *pvParameters) {
    int64_t pulse;
    while (1) {
        if (xQueueReceive(pulse_queue, &pulse, portMAX_DELAY)) {
            // Filter noise
            if (pulse < 200) continue; 
            
            // For now, just a placeholder. 
            // In a real implementation, we would collect bits here.
        }
    }
}

esp_err_t slowrf_init() {
    pulse_queue = xQueueCreate(100, sizeof(int64_t));
    
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .pin_bit_mask = (1ULL << GPIO_GDO0),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
    };
    gpio_config(&io_conf);
    
    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_GDO0, gpio_isr_handler, NULL);
    
    xTaskCreate(slowrf_task, "slowrf_task", 4096, NULL, 15, NULL);
    
    return ESP_OK;
}
