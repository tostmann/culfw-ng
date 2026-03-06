#include "slowrf.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "culfw_parser.h"
#include "cc1101.h"

static const char *TAG = "SLOWRF";
static QueueHandle_t pulse_queue;
static int64_t last_time = 0;
static bool slowrf_debug = false;
static bool slowrf_reporting = false;

void slowrf_set_debug(bool enable) {
    slowrf_debug = enable;
}

void slowrf_set_reporting(bool enable) {
    slowrf_reporting = enable;
}

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    int64_t current_time = esp_timer_get_time();
    int64_t diff = current_time - last_time;
    last_time = current_time;
    
    // Carrier Sense check (High when RSSI > Threshold)
    if (gpio_get_level(GPIO_GDO2) == 0) return;

    if (diff > 150 && diff < 15000) {
        xQueueSendFromISR(pulse_queue, &diff, NULL);
    }
}

#define SLOWRF_SYNC_MIN  8000

typedef struct {
    uint8_t data[16];
    int byte_cnt;
    int bit_cnt;
    uint32_t current_bits;
    int last_bit;
    int pulse_in_bit;
    bool sync_found;
} fs20_dec_t;

typedef struct {
    char s[13];
    int pos;
    int pulse_cnt;
    int64_t pulse_buf[4];
} itv1_dec_t;

typedef struct {
    char s[33];
    int bit_pos;
    int pulse_cnt;
    int64_t pulse_buf[4];
    bool sync_found;
} itv3_dec_t;

static void reset_fs20(fs20_dec_t *dec) {
    memset(dec->data, 0, sizeof(dec->data));
    dec->byte_cnt = 0;
    dec->bit_cnt = 0;
    dec->current_bits = 0;
    dec->last_bit = -1;
    dec->pulse_in_bit = 0;
    dec->sync_found = false;
}

static void reset_itv1(itv1_dec_t *it) {
    it->pos = 0;
    it->pulse_cnt = 0;
    memset(it->s, 0, sizeof(it->s));
}

static void reset_itv3(itv3_dec_t *it) {
    it->bit_pos = 0;
    it->pulse_cnt = 0;
    it->sync_found = false;
    memset(it->s, 0, sizeof(it->s));
}

void slowrf_task(void *pvParameters) {
    int64_t pulse;
    fs20_dec_t fs_dec;
    itv1_dec_t it1_dec;
    itv3_dec_t it3_dec;
    
    reset_fs20(&fs_dec);
    reset_itv1(&it1_dec);
    reset_itv3(&it3_dec);

    while (1) {
        if (xQueueReceive(pulse_queue, &pulse, portMAX_DELAY)) {
            if (slowrf_debug) {
                char d[32];
                int dlen = snprintf(d, sizeof(d), "P:%lld\r\n", (long long)pulse);
                usb_serial_jtag_write_bytes(d, dlen, 0);
            }

            // --- SYNC / END DETECTION ---
            if (pulse > SLOWRF_SYNC_MIN) {
                if (slowrf_reporting) {
                    uint8_t rssi = cc1101_read_rssi();
                    if (fs_dec.byte_cnt >= 4) {
                        char out[64];
                        int len = snprintf(out, sizeof(out), "F");
                        for (int i = 0; i < fs_dec.byte_cnt; i++) {
                            len += snprintf(out + len, sizeof(out) - len, "%02X", fs_dec.data[i]);
                        }
                        len += snprintf(out + len, sizeof(out) - len, "%02X\r\n", rssi);
                        usb_serial_jtag_write_bytes(out, len, 0);
                    }
                    if (it1_dec.pos == 12) {
                        char out[64];
                        int len = snprintf(out, sizeof(out), "is%s%02X\r\n", it1_dec.s, rssi);
                        usb_serial_jtag_write_bytes(out, len, 0);
                    }
                    if (it3_dec.bit_pos == 32) {
                        char out[128];
                        int len = snprintf(out, sizeof(out), "is%s%02X\r\n", it3_dec.s, rssi);
                        usb_serial_jtag_write_bytes(out, len, 0);
                    }
                }
                reset_fs20(&fs_dec);
                reset_itv1(&it1_dec);
                reset_itv3(&it3_dec);
                
                // IT-V3 starts AFTER a long low sync
                if (pulse > 8000 && pulse < 11000) {
                    it3_dec.sync_found = true;
                }
                continue;
            }

            // --- IT-V3 DECODING ---
            if (it3_dec.sync_found) {
                it3_dec.pulse_buf[it3_dec.pulse_cnt++] = pulse;
                if (it3_dec.pulse_cnt == 4) {
                    int64_t p1 = it3_dec.pulse_buf[0];
                    int64_t p2 = it3_dec.pulse_buf[1];
                    int64_t p3 = it3_dec.pulse_buf[2];
                    int64_t p4 = it3_dec.pulse_buf[3];
                    it3_dec.pulse_cnt = 0;
                    
                    #define IS_T_V3(p) (p > 150 && p < 550)
                    #define IS_3T_V3(p) (p >= 650 && p < 1350)

                    if (IS_T_V3(p1) && IS_3T_V3(p2) && IS_T_V3(p3) && IS_3T_V3(p4)) {
                        it3_dec.s[it3_dec.bit_pos++] = '0';
                    } else if (IS_T_V3(p1) && IS_3T_V3(p2) && IS_3T_V3(p3) && IS_T_V3(p4)) {
                        it3_dec.s[it3_dec.bit_pos++] = '1';
                    } else {
                        it3_dec.sync_found = false;
                    }
                    if (it3_dec.bit_pos == 32) it3_dec.sync_found = false;
                }
            }

            // --- IT-V1 DECODING ---
            it1_dec.pulse_buf[it1_dec.pulse_cnt % 4] = pulse;
            it1_dec.pulse_cnt++;
            if (it1_dec.pulse_cnt >= 4) {
                int idx = (it1_dec.pulse_cnt - 4) % 4;
                int64_t p1 = it1_dec.pulse_buf[idx];
                int64_t p2 = it1_dec.pulse_buf[(idx+1)%4];
                int64_t p3 = it1_dec.pulse_buf[(idx+2)%4];
                int64_t p4 = it1_dec.pulse_buf[(idx+3)%4];
                #define IS_T_V1(p) (p >= 200 && p <= 700)
                #define IS_3T_V1(p) (p > 800 && p <= 1750)
                if (it1_dec.pos < 12) {
                    char bit = 0;
                    if (IS_T_V1(p1) && IS_3T_V1(p2) && IS_T_V1(p3) && IS_3T_V1(p4)) bit = '0';
                    else if (IS_3T_V1(p1) && IS_T_V1(p2) && IS_3T_V1(p3) && IS_T_V1(p4)) bit = '1';
                    else if (IS_T_V1(p1) && IS_3T_V1(p2) && IS_3T_V1(p3) && IS_T_V1(p4)) bit = 'F';
                    if (bit) { it1_dec.s[it1_dec.pos++] = bit; it1_dec.pulse_cnt = 0; }
                }
            }

            // --- FS20 DECODING ---
            int fs_bit = -1;
            bool fs_ready = false;
            if (pulse >= 250 && pulse <= 550) { // Half 0
                if (fs_dec.pulse_in_bit == 1 && fs_dec.last_bit == 0) { fs_bit = 0; fs_ready = true; fs_dec.pulse_in_bit = 0; }
                else { fs_dec.last_bit = 0; fs_dec.pulse_in_bit = 1; }
            } else if (pulse > 550 && pulse <= 900) { // Half 1
                if (fs_dec.pulse_in_bit == 1 && fs_dec.last_bit == 1) { fs_bit = 1; fs_ready = true; fs_dec.pulse_in_bit = 0; }
                else { fs_dec.last_bit = 1; fs_dec.pulse_in_bit = 1; }
            } else if (pulse > 900 && pulse <= 1150) { // Full 0
                fs_bit = 0; fs_ready = true; fs_dec.pulse_in_bit = 0;
            } else if (pulse > 1150 && pulse <= 1600) { // Full 1
                fs_bit = 1; fs_ready = true; fs_dec.pulse_in_bit = 0;
            }

            if (fs_ready) {
                if (!fs_dec.sync_found) {
                    if (fs_bit == 1) { fs_dec.sync_found = true; fs_dec.bit_cnt = 0; fs_dec.current_bits = 0; }
                } else {
                    fs_dec.current_bits = (fs_dec.current_bits << 1) | fs_bit;
                    fs_dec.bit_cnt++;
                    if (fs_dec.bit_cnt == 9) {
                        uint8_t val = (fs_dec.current_bits >> 1);
                        uint8_t par = (fs_dec.current_bits & 1);
                        int ones = 0;
                        for (int i=0; i<8; i++) if ((val >> i) & 1) ones++;
                        if (par == (ones % 2)) {
                            if (fs_dec.byte_cnt < sizeof(fs_dec.data)) fs_dec.data[fs_dec.byte_cnt++] = val;
                        } else {
                            if (fs_dec.byte_cnt > 0) reset_fs20(&fs_dec);
                            else fs_dec.sync_found = false;
                        }
                        fs_dec.bit_cnt = 0;
                        fs_dec.current_bits = 0;
                    }
                }
            }
        }
    }
}

esp_err_t slowrf_init() {
    pulse_queue = xQueueCreate(1024, sizeof(int64_t));
    
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .pin_bit_mask = (1ULL << GPIO_GDO0),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
    };
    gpio_config(&io_conf);

    gpio_config_t gdo2_conf = {
        .pin_bit_mask = (1ULL << GPIO_GDO2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
    };
    gpio_config(&gdo2_conf);
    
    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_GDO0, gpio_isr_handler, NULL);
    xTaskCreate(slowrf_task, "slowrf_task", 4096, NULL, 4, NULL);
    
    return ESP_OK;
}
