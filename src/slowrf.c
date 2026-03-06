#include "slowrf.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "culfw_parser.h"

static const char *TAG = "SLOWRF";
static QueueHandle_t pulse_queue;
static int64_t last_time = 0;

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    int64_t current_time = esp_timer_get_time();
    int64_t diff = current_time - last_time;
    last_time = current_time;
    
    // Simple filter for SlowRF: 200us < duration < 15ms
    if (diff > 200 && diff < 15000) { 
        xQueueSendFromISR(pulse_queue, &diff, NULL);
    }
}

void slowrf_task(void *pvParameters) {
    int64_t pulse;
    
    while (1) {
        if (xQueueReceive(pulse_queue, &pulse, portMAX_DELAY)) {
            if (culfw_reporting_enabled()) {
                char out[32];
                // In culfwNG style, we might want to report like SignalDuino
                // but let's just do a simple debug for now
                int len = snprintf(out, sizeof(out), "p%lld\r\n", (long long)pulse);
                usb_serial_jtag_write_bytes(out, len, 0);
            }
            
            // To prevent flooding the console if noise is high, 
            // we should actually only report after a full packet is decoded.
            // But for testing the ISR and the queue, this is fine.
            
            // Periodically yield to prevent WDT if we have high-frequency noise
            static int loop_cnt = 0;
            if (++loop_cnt > 50) {
                vTaskDelay(1);
                loop_cnt = 0;
            }
        }
    }
}

esp_err_t slowrf_init() {
    pulse_queue = xQueueCreate(256, sizeof(int64_t));
    
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
    
    // Lowered priority from 15 to 4 (above LED, below parser)
    xTaskCreate(slowrf_task, "slowrf_task", 4096, NULL, 4, NULL);
    
    return ESP_OK;
}
