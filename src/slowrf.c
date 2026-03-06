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

#define SLOWRF_BIT_S_MIN 300
#define SLOWRF_BIT_S_MAX 550
#define SLOWRF_BIT_L_MIN 550
#define SLOWRF_BIT_L_MAX 900
#define SLOWRF_SYNC_MIN  8000

void slowrf_task(void *pvParameters) {
    int64_t pulse;
    uint32_t bit_acc = 0;
    int bit_count = 0;
    int state = 0; // 0: Idle, 1: Bits
    uint8_t msg[16];
    int msg_byte = 0;
    int msg_bit = 0;

    while (1) {
        if (xQueueReceive(pulse_queue, &pulse, portMAX_DELAY)) {
            if (pulse > SLOWRF_SYNC_MIN) {
                // Potential sync/gap
                if (msg_byte > 3) { // Print if we got some bytes
                    char out[64];
                    int out_len = snprintf(out, sizeof(out), "F");
                    for(int i=0; i<msg_byte; i++) {
                        out_len += snprintf(out + out_len, sizeof(out) - out_len, "%02X", msg[i]);
                    }
                    snprintf(out + out_len, sizeof(out) - out_len, "\r\n");
                    usb_serial_jtag_write_bytes(out, strlen(out), 0);
                }
                msg_byte = 0;
                msg_bit = 0;
                memset(msg, 0, sizeof(msg));
                state = 1;
            } else if (state == 1) {
                int bit = -1;
                if (pulse >= SLOWRF_BIT_S_MIN && pulse <= SLOWRF_BIT_S_MAX) bit = 0;
                else if (pulse >= SLOWRF_BIT_L_MIN && pulse <= SLOWRF_BIT_L_MAX) bit = 1;
                
                if (bit != -1) {
                    // This is very simplified, as FS20 has parity bits
                    // and bits are made of TWO pulses.
                    // Let's assume for now we just want to see if we can gather anything.
                    static int pulse_sub = 0;
                    pulse_sub++;
                    if (pulse_sub == 2) { // 2 pulses per bit
                        pulse_sub = 0;
                        msg[msg_byte] = (msg[msg_byte] << 1) | bit;
                        msg_bit++;
                        if (msg_bit == 8) {
                            msg_bit = 0;
                            msg_byte++;
                            if (msg_byte >= sizeof(msg)) msg_byte = 0;
                        }
                    }
                } else {
                    // Reset on invalid pulse
                    // state = 0;
                }
            }

            // Periodically yield
            static int loop_cnt = 0;
            if (++loop_cnt > 100) {
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
