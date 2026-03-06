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
    uint32_t current_bits;
    int pulse_cnt;
    int64_t pulse_buf[4];
} slowrf_decoder_t;

static void reset_decoder(slowrf_decoder_t *dec) {
    memset(dec->data, 0, sizeof(dec->data));
    dec->byte_cnt = 0;
    dec->bit_cnt = 0;
    dec->current_bits = 0;
    dec->pulse_cnt = 0;
}

typedef struct {
    char s[13];
    int pos;
} it_v1_decoder_t;

static void reset_it_v1(it_v1_decoder_t *it) {
    it->pos = 0;
    memset(it->s, 0, sizeof(it->s));
}

void slowrf_task(void *pvParameters) {
    int64_t pulse;
    slowrf_decoder_t dec;
    it_v1_decoder_t it_dec;
    reset_decoder(&dec);
    reset_it_v1(&it_dec);
    
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
                // FS20 Check
                if (dec.byte_cnt >= 2) {
                    char out[64];
                    int len = snprintf(out, sizeof(out), "F");
                    for (int i = 0; i < dec.byte_cnt; i++) {
                        len += snprintf(out + len, sizeof(out) - len, "%02X", dec.data[i]);
                    }
                    snprintf(out + len, sizeof(out) - len, "\r\n");
                    usb_serial_jtag_write_bytes(out, strlen(out), 0);
                }
                // IT-V1 Check
                if (it_dec.pos == 12) {
                    char out[32];
                    int len = snprintf(out, sizeof(out), "is%s\r\n", it_dec.s);
                    usb_serial_jtag_write_bytes(out, len, 0);
                }
                reset_decoder(&dec);
                reset_it_v1(&it_dec);
                pulse_in_bit = 0;
            } else {
                // 1. Intertechno V1 bit detection (on-the-fly)
                dec.pulse_buf[dec.pulse_cnt % 4] = pulse;
                dec.pulse_cnt++;
                if (dec.pulse_cnt >= 4) {
                    int idx = (dec.pulse_cnt - 4) % 4;
                    int64_t p1 = dec.pulse_buf[idx];
                    int64_t p2 = dec.pulse_buf[(idx+1)%4];
                    int64_t p3 = dec.pulse_buf[(idx+2)%4];
                    int64_t p4 = dec.pulse_buf[(idx+3)%4];
                    
                    #define IS_T(p) (p >= 250 && p <= 650)
                    #define IS_3T(p) (p > 900 && p <= 1600)
                    
                    if (it_dec.pos < 12) {
                        char it_bit = 0;
                        if (IS_T(p1) && IS_3T(p2) && IS_T(p3) && IS_3T(p4)) it_bit = '0';
                        else if (IS_3T(p1) && IS_T(p2) && IS_3T(p3) && IS_T(p4)) it_bit = '1';
                        else if (IS_T(p1) && IS_3T(p2) && IS_3T(p3) && IS_T(p4)) it_bit = 'F';
                        
                        if (it_bit) {
                            it_dec.s[it_dec.pos++] = it_bit;
                            dec.pulse_cnt = 0; // consumed
                        }
                    }
                }

                // 2. FS20 bit detection
                int bit = -1;
                int bit_ready = 0;
                if (pulse >= 250 && pulse <= 520) { // Half-bit 0
                    if (pulse_in_bit == 1 && last_bit == 0) { bit = 0; bit_ready = 1; pulse_in_bit = 0; }
                    else { last_bit = 0; pulse_in_bit = 1; }
                } else if (pulse > 520 && pulse <= 750) { // Half-bit 1
                    if (pulse_in_bit == 1 && last_bit == 1) { bit = 1; bit_ready = 1; pulse_in_bit = 0; }
                    else { last_bit = 1; pulse_in_bit = 1; }
                } else if (pulse > 750 && pulse <= 1000) { // Full-bit 0
                    bit = 0; bit_ready = 1; pulse_in_bit = 0;
                } else if (pulse > 1000 && pulse <= 1450) { // Full-bit 1
                    bit = 1; bit_ready = 1; pulse_in_bit = 0;
                }

                if (bit_ready) {
                    dec.current_bits = (dec.current_bits << 1) | bit;
                    dec.bit_cnt++;
                    if (dec.bit_cnt == 9) {
                        uint8_t data_byte = (dec.current_bits >> 1);
                        uint8_t parity_bit = (dec.current_bits & 1);
                        int ones = 0;
                        for (int i = 0; i < 8; i++) { if ((data_byte >> i) & 1) ones++; }
                        // FS20 ODD Parity
                        if (parity_bit == ((ones % 2) ? 0 : 1)) {
                            if (dec.byte_cnt < sizeof(dec.data)) { 
                                dec.data[dec.byte_cnt++] = data_byte; 
                            }
                        } else { 
                            // Only reset if we already had some data, otherwise it's just noise
                            if (dec.byte_cnt > 0) {
                                if (slowrf_debug) usb_serial_jtag_write_bytes("PARERR\r\n", 8, 0);
                                reset_decoder(&dec); 
                            } else {
                                dec.bit_cnt = 0;
                                dec.current_bits = 0;
                            }
                        }
                        dec.current_bits = 0; dec.bit_cnt = 0;
                    }
                }
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
