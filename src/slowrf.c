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
static bool slowrf_debug = false;

void slowrf_set_debug(bool enable) {
    slowrf_debug = enable;
}

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

typedef struct {
    uint8_t data[16];
    int byte_cnt;
    int bit_cnt;
    uint8_t current_byte;
} slowrf_decoder_t;

static void reset_decoder(slowrf_decoder_t *dec) {
    memset(dec->data, 0, sizeof(dec->data));
    dec->byte_cnt = 0;
    dec->bit_cnt = 0;
    dec->current_byte = 0;
}

void slowrf_task(void *pvParameters) {
    int64_t pulse;
    slowrf_decoder_t dec;
    reset_decoder(&dec);
    
    int pulse_in_bit = 0;
    int last_bit = -1;

    while (1) {
        if (xQueueReceive(pulse_queue, &pulse, portMAX_DELAY)) {
            if (slowrf_debug) {
                char d[32];
                int dlen = snprintf(d, sizeof(d), "P: %lld\r\n", (long long)pulse);
                usb_serial_jtag_write_bytes(d, dlen, 0);
            }
            if (pulse > SLOWRF_SYNC_MIN) {
                if (dec.byte_cnt >= 2) {
                    char out[64];
                    int len = snprintf(out, sizeof(out), "F");
                    for (int i = 0; i < dec.byte_cnt; i++) {
                        len += snprintf(out + len, sizeof(out) - len, "%02X", dec.data[i]);
                    }
                    snprintf(out + len, sizeof(out) - len, "\r\n");
                    usb_serial_jtag_write_bytes(out, strlen(out), 0);
                }
                reset_decoder(&dec);
                pulse_in_bit = 0;
            } else {
                int bit = -1;
                // Wider tolerances for noise
                if (pulse >= 250 && pulse <= 550) bit = 0;
                else if (pulse >= 550 && pulse <= 950) bit = 1;

                if (bit != -1) {
                    if (pulse_in_bit == 0) {
                        last_bit = bit;
                        pulse_in_bit = 1;
                    } else {
                        if (bit == last_bit) {
                            // Valid FS20 bit (two identical pulses)
                            dec.current_byte = (dec.current_byte << 1) | bit;
                            dec.bit_cnt++;
                            
                            // FS20: 8 data bits + 1 parity bit = 9 bits
                            if (dec.bit_cnt == 9) {
                                uint8_t data_byte = (dec.current_byte >> 1);
                                uint8_t parity_bit = (dec.current_byte & 1);
                                int ones = 0;
                                for (int i = 0; i < 8; i++) {
                                    if ((data_byte >> i) & 1) ones++;
                                }
                                if ((ones % 2) == parity_bit) {
                                    if (dec.byte_cnt < sizeof(dec.data)) {
                                        dec.data[dec.byte_cnt++] = data_byte;
                                    }
                                } else {
                                    // Parity error, discard packet
                                    reset_decoder(&dec);
                                }
                                dec.current_byte = 0;
                                dec.bit_cnt = 0;
                            }
                        }
                        pulse_in_bit = 0;
                    }
                } else {
                    pulse_in_bit = 0;
                }
            }

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
