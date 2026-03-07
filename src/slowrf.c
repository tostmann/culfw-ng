#include "slowrf.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "culfw_parser.h"
#include "cc1101.h"
#include <string.h>

static const char *TAG = "SLOWRF";

typedef struct {
    uint16_t duration;
    uint8_t level;
} pulse_t;

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
    int diff = (int)(current_time - last_time);
    last_time = current_time;
    
    int level = gpio_get_level(GPIO_GDO0);

    // Carrier Sense check (High when RSSI > Threshold)
    if (gpio_get_level(GPIO_GDO2) == 0) return;

    if (diff > 100 && diff < 20000) {
        pulse_t p = { .duration = (uint16_t)diff, .level = (uint8_t)level };
        xQueueSendFromISR(pulse_queue, &p, NULL);
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
    uint8_t data[16];
    int byte_cnt;
    int bit_cnt;
    uint32_t current_bits;
    int pulse_state;
    bool sync_found;
} fht_dec_t;

typedef struct {
    char s[17];
    int pos;
    int pulse_cnt;
    uint16_t pulse_buf[4];
} itv1_dec_t;

typedef struct {
    char s[33];
    int bit_pos;
    int pulse_cnt;
    uint16_t pulse_buf[4];
    bool sync_found;
} itv3_dec_t;

typedef struct {
    uint8_t nibbles[24];
    int nibble_cnt;
    int bit_cnt;
    uint8_t current_nibble;
    int pulse_state; 
    int64_t last_pulse;
    bool sync_found;
} sensor_dec_t;

typedef struct {
    uint8_t data[16];
    int nibble_cnt;
    int bit_cnt;
    bool sync_found;
    int pulse_state;
} os_dec_t;

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

static void reset_sensor(sensor_dec_t *sd) {
    memset(sd->nibbles, 0, sizeof(sd->nibbles));
    sd->nibble_cnt = 0;
    sd->bit_cnt = 0;
    sd->current_nibble = 0;
    sd->pulse_state = 0;
    sd->sync_found = false;
}

static void reset_os(os_dec_t *dec) {
    memset(dec->data, 0, sizeof(dec->data));
    dec->nibble_cnt = 0;
    dec->bit_cnt = 0;
    dec->sync_found = false;
    dec->pulse_state = 0;
}

static void reset_fht(fht_dec_t *dec) {
    memset(dec->data, 0, sizeof(dec->data));
    dec->byte_cnt = 0;
    dec->bit_cnt = 0;
    dec->current_bits = 0;
    dec->pulse_state = 0;
    dec->sync_found = false;
}

void slowrf_task(void *pvParameters) {
    pulse_t p_in;
    fs20_dec_t fs_dec;
    fht_dec_t fht_dec;
    itv1_dec_t it1_dec;
    itv3_dec_t it3_dec;
    sensor_dec_t hms_dec;
    sensor_dec_t s300_dec;
    os_dec_t os_dec;
    
    reset_fs20(&fs_dec);
    reset_fht(&fht_dec);
    reset_itv1(&it1_dec);
    reset_itv3(&it3_dec);
    reset_sensor(&hms_dec);
    reset_sensor(&s300_dec);
    reset_os(&os_dec);

    bool it3_last_sync = false;

    while (1) {
        if (xQueueReceive(pulse_queue, &p_in, portMAX_DELAY)) {
            uint16_t pulse = p_in.duration;
            uint8_t level = p_in.level;

            if (slowrf_debug) {
                char d[32];
                int dlen = snprintf(d, sizeof(d), "P:%u L:%u\r\n", pulse, level);
                usb_serial_jtag_write_bytes(d, dlen, 0);
            }

            // --- SYNC / END DETECTION ---
            if (pulse > SLOWRF_SYNC_MIN) {
                if (slowrf_reporting) {
                    uint8_t rssi = cc1101_read_rssi();

                    if (fs_dec.byte_cnt >= 4) {
                        char out[64];
                        int len = snprintf(out, sizeof(out), "F");
                        for (int i = 0; i < fs_dec.byte_cnt; i++) len += snprintf(out + len, sizeof(out) - len, "%02X", fs_dec.data[i]);
                        len += snprintf(out + len, sizeof(out) - len, "%02X\r\n", rssi);
                        usb_serial_jtag_write_bytes(out, len, 0);
                    }
                    if (it1_dec.pos == 12 && !it3_last_sync) {
                        char out[64];
                        int len = snprintf(out, sizeof(out), "is%s%02X\r\n", it1_dec.s, rssi);
                        usb_serial_jtag_write_bytes(out, len, 0);
                    }
                    if (it3_dec.bit_pos == 32) {
                        char out[128];
                        int len = snprintf(out, sizeof(out), "is%s%02X\r\n", it3_dec.s, rssi);
                        usb_serial_jtag_write_bytes(out, len, 0);
                    }
                    if (os_dec.nibble_cnt >= 16) {
                        char out[128];
                        int len = snprintf(out, sizeof(out), "P");
                        for (int i=0; i < os_dec.nibble_cnt; i++) {
                            int idx = i / 2;
                            uint8_t n = (i % 2 == 0) ? (os_dec.data[idx] & 0xF) : (os_dec.data[idx] >> 4);
                            len += snprintf(out + len, sizeof(out) - len, "%X", n);
                        }
                        len += snprintf(out + len, sizeof(out) - len, "%02X\r\n", rssi);
                        usb_serial_jtag_write_bytes(out, len, 0);
                    }
                    if (hms_dec.nibble_cnt >= 19) {
                        char out[64];
                        int len = snprintf(out, sizeof(out), "H");
                        for(int i=0; i<hms_dec.nibble_cnt; i++) len += snprintf(out+len, sizeof(out)-len, "%X", hms_dec.nibbles[i]);
                        len += snprintf(out+len, sizeof(out)-len, "%02X\r\n", rssi);
                        usb_serial_jtag_write_bytes(out, len, 0);
                    }
                    if (s300_dec.nibble_cnt >= 9) {
                        char out[64];
                        int len = snprintf(out, sizeof(out), "K");
                        for(int i=0; i<s300_dec.nibble_cnt; i++) len += snprintf(out+len, sizeof(out)-len, "%X", s300_dec.nibbles[i]);
                        len += snprintf(out+len, sizeof(out)-len, "%02X\r\n", rssi);
                        usb_serial_jtag_write_bytes(out, len, 0);
                    }
                    if (fht_dec.byte_cnt >= 5) {
                        char out[64];
                        int len = snprintf(out, sizeof(out), "T");
                        for (int i = 0; i < fht_dec.byte_cnt; i++) len += snprintf(out + len, sizeof(out) - len, "%02X", fht_dec.data[i]);
                        len += snprintf(out + len, sizeof(out) - len, "%02X\r\n", rssi);
                        usb_serial_jtag_write_bytes(out, len, 0);
                    }
                }
                reset_fs20(&fs_dec);
                reset_fht(&fht_dec);
                reset_itv1(&it1_dec);
                reset_itv3(&it3_dec);
                reset_sensor(&hms_dec);
                reset_sensor(&s300_dec);
                reset_os(&os_dec);
                
                if (pulse > 8000 && pulse < 11000) {
                    it3_dec.sync_found = true;
                    it3_last_sync = true;
                } else {
                    it3_last_sync = false;
                }
                continue;
            }

            // --- OREGON SCIENTIFIC (V2/V3) ---
            if (pulse > 200 && pulse < 1300) {
                if (!os_dec.sync_found) {
                    // Preamble: many short pulses (approx 500us)
                    if (pulse > 350 && pulse < 650) {
                        if (++os_dec.pulse_state > 12) {
                            os_dec.sync_found = true;
                            os_dec.nibble_cnt = 0; os_dec.bit_cnt = 0; os_dec.pulse_state = 0;
                        }
                    } else os_dec.pulse_state = 0;
                } else {
                    // Manchester Decoding for OS
                    // T ~ 500us. Short = T, Long = 2T.
                    int bit = -1;
                    if (pulse < 750) { // Short pulse (T)
                        if (os_dec.pulse_state == 1) {
                            bit = (level == 0) ? 1 : 0; // OS uses inverted Manchester or specific edge
                            os_dec.pulse_state = 0;
                        } else {
                            os_dec.pulse_state = 1;
                        }
                    } else if (pulse < 1250) { // Long pulse (2T)
                        bit = (level == 0) ? 1 : 0;
                        os_dec.pulse_state = 1; // Reset to 1 because 2T contains a mid-bit transition
                    }

                    if (bit != -1) {
                        if (os_dec.nibble_cnt < 20) {
                            // OS sends LSB first in nibbles
                            if (bit) os_dec.data[os_dec.nibble_cnt/2] |= (1 << (os_dec.bit_cnt + (os_dec.nibble_cnt % 2 ? 4 : 0)));
                            else     os_dec.data[os_dec.nibble_cnt/2] &= ~(1 << (os_dec.bit_cnt + (os_dec.nibble_cnt % 2 ? 4 : 0)));
                            
                            if (++os_dec.bit_cnt == 4) {
                                os_dec.bit_cnt = 0;
                                // Check for OS Sync Nibble 0xA (LSB: 0101)
                                if (os_dec.nibble_cnt == 0 && (os_dec.data[0] & 0xF) != 0xA) {
                                    os_dec.sync_found = false; // False sync
                                }
                                os_dec.nibble_cnt++;
                            }
                        }
                    }
                }
            }

            // --- IT-V3 DECODING ---
            if (it3_dec.sync_found) {
                it3_dec.pulse_buf[it3_dec.pulse_cnt++] = pulse;
                if (it3_dec.pulse_cnt == 4) {
                    uint16_t p1 = it3_dec.pulse_buf[0], p2 = it3_dec.pulse_buf[1], p3 = it3_dec.pulse_buf[2], p4 = it3_dec.pulse_buf[3];
                    it3_dec.pulse_cnt = 0;
                    #define IS_T_V3(p) (p > 150 && p < 550)
                    #define IS_3T_V3(p) (p >= 650 && p < 1350)
                    if (IS_T_V3(p1) && IS_3T_V3(p2) && IS_T_V3(p3) && IS_3T_V3(p4)) it3_dec.s[it3_dec.bit_pos++] = '0';
                    else if (IS_T_V3(p1) && IS_3T_V3(p2) && IS_3T_V3(p3) && IS_T_V3(p4)) it3_dec.s[it3_dec.bit_pos++] = '1';
                    else it3_dec.sync_found = false;
                    if (it3_dec.bit_pos == 32) it3_dec.sync_found = false;
                }
            }

            // --- IT-V1 DECODING ---
            it1_dec.pulse_buf[it1_dec.pulse_cnt % 4] = pulse;
            it1_dec.pulse_cnt++;
            if (it1_dec.pulse_cnt >= 4) {
                int idx = (it1_dec.pulse_cnt - 4) % 4;
                uint16_t p1 = it1_dec.pulse_buf[idx], p2 = it1_dec.pulse_buf[(idx + 1) % 4], p3 = it1_dec.pulse_buf[(idx + 2) % 4], p4 = it1_dec.pulse_buf[(idx + 3) % 4];
                #define IS_T_V1(p) (p >= 150 && p <= 750)
                #define IS_3T_V1(p) (p > 800 && p <= 1850)
                if (it1_dec.pos < 16) {
                    char bit = 0;
                    if (IS_T_V1(p1) && IS_3T_V1(p2) && IS_T_V1(p3) && IS_3T_V1(p4)) bit = '0';
                    else if (IS_3T_V1(p1) && IS_T_V1(p2) && IS_3T_V1(p3) && IS_T_V1(p4)) bit = '1';
                    else if (IS_T_V1(p1) && IS_3T_V1(p2) && IS_3T_V1(p3) && IS_T_V1(p4)) bit = 'F';
                    if (bit) { it1_dec.s[it1_dec.pos++] = bit; it1_dec.pulse_cnt = 0; }
                }
            }

            // --- FS20 DECODING ---
            int fs_bit = -1; bool fs_ready = false;
            if (pulse >= 250 && pulse <= 550) { 
                if (fs_dec.pulse_in_bit == 1 && fs_dec.last_bit == 0) { fs_bit = 0; fs_ready = true; fs_dec.pulse_in_bit = 0; }
                else { fs_dec.last_bit = 0; fs_dec.pulse_in_bit = 1; }
            } else if (pulse > 550 && pulse <= 900) { 
                if (fs_dec.pulse_in_bit == 1 && fs_dec.last_bit == 1) { fs_bit = 1; fs_ready = true; fs_dec.pulse_in_bit = 0; }
                else { fs_dec.last_bit = 1; fs_dec.pulse_in_bit = 1; }
            } else if (pulse > 900 && pulse <= 1150) { fs_bit = 0; fs_ready = true; fs_dec.pulse_in_bit = 0; }
            else if (pulse > 1150 && pulse <= 1600) { fs_bit = 1; fs_ready = true; fs_dec.pulse_in_bit = 0; }

            if (fs_ready) {
                if (!fs_dec.sync_found) { if (fs_bit == 1) { fs_dec.sync_found = true; fs_dec.bit_cnt = 0; fs_dec.current_bits = 0; } }
                else {
                    fs_dec.current_bits = (fs_dec.current_bits << 1) | fs_bit;
                    if (++fs_dec.bit_cnt == 9) {
                        uint8_t val = (fs_dec.current_bits >> 1), par = (fs_dec.current_bits & 1);
                        int ones = 0; for (int i=0; i<8; i++) if ((val >> i) & 1) ones++;
                        if (par == (ones % 2)) { if (fs_dec.byte_cnt < 16) fs_dec.data[fs_dec.byte_cnt++] = val; }
                        else { if (fs_dec.byte_cnt > 0) reset_fs20(&fs_dec); else fs_dec.sync_found = false; }
                        fs_dec.bit_cnt = 0; fs_dec.current_bits = 0;
                    }
                }
            }

            // --- HMS / S300TH ---
            if (!s300_dec.sync_found && pulse > 950 && pulse < 1350) { reset_sensor(&s300_dec); s300_dec.sync_found = true; }
            else if (s300_dec.sync_found) {
                if (s300_dec.pulse_state == 0) { s300_dec.last_pulse = pulse; s300_dec.pulse_state = 1; }
                else {
                    int bit = -1;
                    if (s300_dec.last_pulse < 600 && pulse > 600) bit = 0;
                    else if (s300_dec.last_pulse < 600 && pulse < 600) bit = 1;
                    if (bit != -1) {
                        s300_dec.current_nibble |= (bit << s300_dec.bit_cnt);
                        if (++s300_dec.bit_cnt == 4) { if (s300_dec.nibble_cnt < 24) s300_dec.nibbles[s300_dec.nibble_cnt++] = s300_dec.current_nibble; s300_dec.current_nibble = 0; s300_dec.bit_cnt = 0; }
                    } else { if (s300_dec.nibble_cnt < 9) reset_sensor(&s300_dec); else s300_dec.sync_found = false; }
                    s300_dec.pulse_state = 0;
                }
            }
            if (hms_dec.pulse_state == 0) { if (pulse >= 300 && pulse <= 900) { hms_dec.last_pulse = pulse; hms_dec.pulse_state = 1; } else if (hms_dec.nibble_cnt > 0) reset_sensor(&hms_dec); }
            else {
                if (pulse >= 300 && pulse <= 600) {
                    int bit = (hms_dec.last_pulse > 600) ? 1 : 0;
                    hms_dec.current_nibble = (hms_dec.current_nibble << 1) | bit;
                    if (++hms_dec.bit_cnt == 4) { if (hms_dec.nibble_cnt < 24) hms_dec.nibbles[hms_dec.nibble_cnt++] = hms_dec.current_nibble; hms_dec.current_nibble = 0; hms_dec.bit_cnt = 0; }
                    hms_dec.pulse_state = 0;
                } else { if (hms_dec.nibble_cnt > 0) reset_sensor(&hms_dec); else hms_dec.pulse_state = 0; }
            }
        }
    }
}

esp_err_t slowrf_init() {
    pulse_queue = xQueueCreate(1024, sizeof(pulse_t));
    gpio_config_t io_conf = { .intr_type = GPIO_INTR_ANYEDGE, .pin_bit_mask = (1ULL << GPIO_GDO0), .mode = GPIO_MODE_INPUT, .pull_up_en = 0, .pull_down_en = 0 };
    gpio_config(&io_conf);
    gpio_config_t gdo2_conf = { .pin_bit_mask = (1ULL << GPIO_GDO2), .mode = GPIO_MODE_INPUT, .pull_up_en = 0, .pull_down_en = 0 };
    gpio_config(&gdo2_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_GDO0, gpio_isr_handler, NULL);
    xTaskCreate(slowrf_task, "slowrf_task", 4096, NULL, 4, NULL);
    return ESP_OK;
}
