#include "slowrf.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "culfw_parser.h"
#include "cc1101.h"
#include "matter_bridge.h"
#include "generic_decoder.h"
#include <string.h>

static const char *TAG = "SLOWRF";

typedef struct {
    uint16_t duration;
    uint8_t level;
} pulse_t;

static QueueHandle_t pulse_queue;
static int64_t last_time = 0;
static bool slowrf_debug = false;
static bool slowrf_reporting = true; // Default ON
static uint8_t slowrf_mode = SLOWRF_MODE_CUL;
static bool protocol_matched = false;

static void slowrf_output_packet(const char* prefix, const char* data, uint8_t rssi) {
    slowrf_mark_matched();
    char out[128];
    int len = 0;
    if (slowrf_mode == SLOWRF_MODE_SIGNALDUINO) {
        // Simple mapping for SignalDuino
        len = snprintf(out, sizeof(out), "MS;P0=0;D=%s;RSS=%d;\r\n", data, rssi);
    } else {
        len = snprintf(out, sizeof(out), "%s%s%02X\r\n", prefix, data, rssi);
    }
    usb_serial_jtag_write_bytes(out, len, 0);
    
    // Web Event
    char web_msg[64];
    snprintf(web_msg, sizeof(web_msg), "%s%s (%d)", prefix, data, rssi);
    slowrf_add_web_event(web_msg);
}

#define MAX_WEB_EVENTS 10
typedef struct {
    char msg[64];
    int64_t timestamp;
} web_event_t;
static web_event_t web_events[MAX_WEB_EVENTS];
static int web_event_idx = 0;

void slowrf_add_web_event(const char* msg) {
    strncpy(web_events[web_event_idx].msg, msg, 63);
    web_events[web_event_idx].msg[63] = 0;
    web_events[web_event_idx].timestamp = esp_timer_get_time() / 1000000;
    web_event_idx = (web_event_idx + 1) % MAX_WEB_EVENTS;
}

int slowrf_get_web_events(char* buf, int max_len) {
    int len = 0;
    // Iterate backwards from the last added event
    for (int i = 0; i < MAX_WEB_EVENTS; i++) {
        int idx = (web_event_idx - 1 - i + MAX_WEB_EVENTS) % MAX_WEB_EVENTS;
        if (web_events[idx].msg[0] != 0) {
            len += snprintf(buf + len, max_len - len, "[%llds] %s<br>", web_events[idx].timestamp, web_events[idx].msg);
        }
    }
    return len;
}

#define MU_BUFFER_SIZE 256
static int16_t mu_buffer[MU_BUFFER_SIZE];
static uint16_t mu_idx = 0;

static void flush_mu_buffer() {
    if (mu_idx == 0) return;
    if (slowrf_mode != SLOWRF_MODE_SIGNALDUINO || !slowrf_reporting || protocol_matched) {
        mu_idx = 0;
        protocol_matched = false;
        return;
    }
    
    char out[1024]; // Large enough for pulses
    int len = snprintf(out, sizeof(out), "MU;L=%d;D=", mu_idx);
    for (int i = 0; i < mu_idx; i++) {
        len += snprintf(out + len, sizeof(out) - len, "%d,", mu_buffer[i]);
        if (len > sizeof(out) - 20) break; // Avoid overflow
    }
    len += snprintf(out + len, sizeof(out) - len, ";RSS=%d;\r\n", cc1101_read_rssi());
    usb_serial_jtag_write_bytes(out, len, 0);
    mu_idx = 0;
    protocol_matched = false;
}

void slowrf_set_debug(bool enable) {
    slowrf_debug = enable;
}

void slowrf_set_reporting(bool enable) {
    slowrf_reporting = enable;
}

void slowrf_set_mode(uint8_t mode) {
    if (mode == SLOWRF_MODE_CUL || mode == SLOWRF_MODE_SIGNALDUINO) {
        slowrf_mode = mode;
        ESP_LOGI(TAG, "Switched to mode X%02X", mode);
    }
}

uint8_t slowrf_get_mode() {
    return slowrf_mode;
}

void slowrf_mark_matched() {
    protocol_matched = true;
    mu_idx = 0; // Clear MU buffer if protocol matched
}

void slowrf_process_pulse(uint16_t duration, uint8_t level) {
    pulse_t p = { .duration = duration, .level = level };
    xQueueSend(pulse_queue, &p, 0);
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
    uint32_t bit_buffer;
    int bit_cnt;
    int pulse_cnt;
} rtl433_dec_t;

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

static void reset_rtl433(rtl433_dec_t *dec) {
    dec->bit_buffer = 0;
    dec->bit_cnt = 0;
    dec->pulse_cnt = 0;
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
    rtl433_dec_t rtl_dec;
    
    reset_fs20(&fs_dec);
    reset_fht(&fht_dec);
    reset_itv1(&it1_dec);
    reset_itv3(&it3_dec);
    reset_sensor(&hms_dec);
    reset_sensor(&s300_dec);
    reset_os(&os_dec);
    reset_rtl433(&rtl_dec);

    bool it3_last_sync = false;

    // Initialize generic decoder
    generic_decoder_init();

    while (1) {
        if (xQueueReceive(pulse_queue, &p_in, portMAX_DELAY)) {
            uint16_t pulse = p_in.duration;
            // p_in.level is current pin state, so pulse was !level
            uint8_t pulse_level = !p_in.level;
            
            // Feed Generic Decoder
            generic_decoder_process_pulse(pulse, pulse_level);

            uint8_t level = p_in.level;

            // Update MU buffer
            if (mu_idx < MU_BUFFER_SIZE) {
                // SignalDuino uses positive for high, negative for low
                mu_buffer[mu_idx++] = (pulse_level ? (int16_t)pulse : -(int16_t)pulse);
            }

            if (slowrf_debug) {
                char d[32];
                int dlen = snprintf(d, sizeof(d), "P:%u L:%u\r\n", pulse, level);
                usb_serial_jtag_write_bytes(d, dlen, 0);
            }

            // --- SYNC / END DETECTION ---
            if (pulse > SLOWRF_SYNC_MIN) {
                flush_mu_buffer();
                if (slowrf_reporting) {
                    uint8_t rssi = cc1101_read_rssi();

                    if (fs_dec.byte_cnt >= 4) {
                        char d[32]; char id[16];
                        snprintf(id, sizeof(id), "F%02X%02X%02X", fs_dec.data[0], fs_dec.data[1], fs_dec.data[2]);
                        int dlen = 0;
                        for (int i = 0; i < fs_dec.byte_cnt; i++) dlen += snprintf(d + dlen, sizeof(d) - dlen, "%02X", fs_dec.data[i]);
                        slowrf_output_packet("F", d, rssi);
                        matter_bridge_report_event(id, "FS20", DEVICE_TYPE_SWITCH, (fs_dec.data[3] & 0x1) ? 1.0 : 0.0);
                    }
                    // IT_V1 Check: Removed !it3_last_sync because CUL sends ~10000us sync which overlaps with IT_V3 sync range
                    if (it1_dec.pos == 12) {
                        slowrf_output_packet("is", it1_dec.s, rssi);
                        char id[17]; strcpy(id, it1_dec.s);
                        id[11] = 'X'; // Mask state bit in ID
                        matter_bridge_report_event(id, "IT_V1", DEVICE_TYPE_SWITCH, (it1_dec.s[11] == '0' ? 0.0 : 1.0));
                    }
                    if (it3_dec.bit_pos == 32) {
                        slowrf_output_packet("is", it3_dec.s, rssi);
                        char id[33]; strcpy(id, it3_dec.s);
                        id[18] = 'X'; // Mask state bit in ID
                        matter_bridge_report_event(id, "IT_V3", DEVICE_TYPE_SWITCH, (it3_dec.s[18] == '1' ? 1.0 : 0.0));
                    }
                    if (os_dec.nibble_cnt >= 16) {
                        char d[64]; int dlen = 0;
                        for (int i=0; i < os_dec.nibble_cnt; i++) {
                            int idx = i / 2;
                            uint8_t n = (i % 2 == 0) ? (os_dec.data[idx] & 0xF) : (os_dec.data[idx] >> 4);
                            dlen += snprintf(d + dlen, sizeof(d) - dlen, "%X", n);
                        }
                        slowrf_output_packet("P", d, rssi);
                        // Oregon: ID is usually in the first few nibbles
                        char id[17]; strncpy(id, d, 16); id[16] = 0;
                        matter_bridge_report_event(id, "Oregon", DEVICE_TYPE_TEMP_SENSOR, 20.0); // Placeholder
                    }
                    if (hms_dec.nibble_cnt >= 8) {
                        char d[32]; int dlen = 0;
                        for(int i=0; i<hms_dec.nibble_cnt; i++) dlen += snprintf(d+dlen, sizeof(d)-dlen, "%X", hms_dec.nibbles[i]);
                        slowrf_output_packet("H", d, rssi);
                        char id[17]; strncpy(id, d, 16); id[16] = 0;
                        
                        // Extract value from nibbles 6 and 7 (if available) for testing
                        float val = 20.0;
                        if (hms_dec.nibble_cnt >= 8) {
                            val = (float)((hms_dec.nibbles[6] << 4) | hms_dec.nibbles[7]);
                        }
                        matter_bridge_report_event(id, "HMS", DEVICE_TYPE_TEMP_SENSOR, val);
                    }
                    if (s300_dec.nibble_cnt >= 9) {
                        char d[32]; int dlen = 0;
                        for(int i=0; i<s300_dec.nibble_cnt; i++) dlen += snprintf(d+dlen, sizeof(d)-dlen, "%X", s300_dec.nibbles[i]);
                        slowrf_output_packet("K", d, rssi);
                        char id[17]; strncpy(id, d, 16); id[16] = 0;
                        matter_bridge_report_event(id, "S300TH", DEVICE_TYPE_TEMP_SENSOR, 20.0); // Placeholder
                    }
                    if (fht_dec.byte_cnt >= 5) {
                        char d[32]; char id[16];
                        snprintf(id, sizeof(id), "T%02X%02X%02X", fht_dec.data[0], fht_dec.data[1], fht_dec.data[2]);
                        int dlen = 0;
                        for (int i = 0; i < fht_dec.byte_cnt; i++) dlen += snprintf(d + dlen, sizeof(d) - dlen, "%02X", fht_dec.data[i]);
                        slowrf_output_packet("T", d, rssi);
                        // FHT reporting - data[3] is the value byte (0-255), represents valve % or temp*2
                        matter_bridge_report_event(id, "FHT", DEVICE_TYPE_TEMP_SENSOR, (float)fht_dec.data[3] / 2.0f);
                    }
                    /* if (rtl_dec.bit_cnt >= 24) {
                        char d[32];
                        snprintf(d, sizeof(d), "%08X", (unsigned int)rtl_dec.bit_buffer);
                        slowrf_output_packet("r", d, rssi);
                        matter_bridge_report_event(d, "Generic", DEVICE_TYPE_CONTACT_SENSOR, 1.0);
                    } */
                }
                reset_fs20(&fs_dec);
                reset_fht(&fht_dec);
                reset_itv1(&it1_dec);
                reset_itv3(&it3_dec);
                reset_sensor(&hms_dec);
                reset_sensor(&s300_dec);
                reset_os(&os_dec);
                reset_rtl433(&rtl_dec);
                
                if (pulse > 8000 && pulse < 11000) {
                    it3_dec.sync_found = true;
                    it3_last_sync = true;
                } else {
                    it3_last_sync = false;
                }
                continue;
            }

            // --- OREGON SCIENTIFIC (V2/V3) ---
            if (pulse > 200 && pulse < 1400) {
                if (!os_dec.sync_found) {
                    if (pulse > 350 && pulse < 750) { 
                        if (++os_dec.pulse_state > 10) {
                            os_dec.sync_found = true;
                            os_dec.nibble_cnt = 0; os_dec.bit_cnt = 0; os_dec.pulse_state = 0;
                            memset(os_dec.data, 0, sizeof(os_dec.data));
                        }
                    } else os_dec.pulse_state = 0;
                } else {
                    int bit = -1;
                    if (pulse < 850) { // Short
                        if (os_dec.pulse_state == 1) {
                            bit = (level == 0) ? 1 : 0; // Fall=1, Rise=0
                            os_dec.pulse_state = 0;
                        } else os_dec.pulse_state = 1;
                    } else if (pulse < 1400) { // Long
                        bit = (level == 0) ? 1 : 0;
                        os_dec.pulse_state = 1;
                    }
                    if (bit != -1) {
                        if (os_dec.nibble_cnt < 24) {
                            if (bit) os_dec.data[os_dec.nibble_cnt/2] |= (1 << (os_dec.bit_cnt + (os_dec.nibble_cnt % 2 ? 4 : 0)));
                            if (++os_dec.bit_cnt == 4) {
                                os_dec.bit_cnt = 0;
                                // Sync nibble check 0xA (LSB first: 0101)
                                if (os_dec.nibble_cnt == 0 && (os_dec.data[0] & 0xF) != 0xA) {
                                    os_dec.sync_found = false;
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

            // --- FHT DECODING ---
            int fht_bit = -1;
            if (pulse > 350 && pulse < 850) {
                if (level == 0) { // FHT bits are characterized by the space after the pulse
                    if (pulse > 550) fht_bit = 0; // approx 600us
                    else fht_bit = 1;            // approx 400us
                }
            }

            if (fht_bit != -1) {
                if (!fht_dec.sync_found) {
                    fht_dec.current_bits = (fht_dec.current_bits << 1) | fht_bit;
                    if ((fht_dec.current_bits & 0xFFF) == 0x0C) { // FHT sync pattern
                        fht_dec.sync_found = true;
                        fht_dec.bit_cnt = 0; fht_dec.byte_cnt = 0; fht_dec.current_bits = 0;
                    }
                } else {
                    fht_dec.current_bits = (fht_dec.current_bits << 1) | fht_bit;
                    if (++fht_dec.bit_cnt == 9) {
                        fht_dec.data[fht_dec.byte_cnt++] = (uint8_t)(fht_dec.current_bits >> 1);
                        fht_dec.bit_cnt = 0; fht_dec.current_bits = 0;
                        if (fht_dec.byte_cnt >= 16) fht_dec.sync_found = false;
                    }
                }
            }

            // --- HMS / S300TH ---
            // S300TH: Sync ~1000us High. Bit 0: 400us High / 800us Low. Bit 1: 400us High / 400us Low.
            if (level == 1 && pulse > 800 && pulse < 1500) { // End of High Sync
                reset_sensor(&s300_dec); s300_dec.sync_found = true;
            } else if (s300_dec.sync_found) {
                if (level == 0) { // End of High
                    s300_dec.last_pulse = pulse;
                } else { // End of Low
                    int bit = -1;
                    if (s300_dec.last_pulse < 600) {
                        if (pulse > 600) bit = 0;
                        else bit = 1;
                    }
                    if (bit != -1) {
                        s300_dec.current_nibble |= (bit << s300_dec.bit_cnt);
                        if (++s300_dec.bit_cnt == 4) {
                            if (s300_dec.nibble_cnt < 24) s300_dec.nibbles[s300_dec.nibble_cnt++] = s300_dec.current_nibble;
                            s300_dec.current_nibble = 0; s300_dec.bit_cnt = 0;
                        }
                    } else {
                        if (s300_dec.nibble_cnt < 9) reset_sensor(&s300_dec);
                        else s300_dec.sync_found = false;
                    }
                }
            }

            // HMS: Bit 0: 400us High / 400us Low. Bit 1: 800us High / 400us Low.
            if (level == 0) { // End of High
                hms_dec.last_pulse = pulse;
                hms_dec.pulse_state = 1;
            } else if (hms_dec.pulse_state == 1) { // End of Low
                if (pulse >= 200 && pulse <= 1200) {
                    int bit = -1;
                    if (hms_dec.last_pulse > 600 && hms_dec.last_pulse < 1200) bit = 1;
                    else if (hms_dec.last_pulse > 200 && hms_dec.last_pulse <= 600) bit = 0;

                    if (bit != -1) {
                        // Skip leading zeros (preamble)
                        if (bit == 1 || hms_dec.nibble_cnt > 0 || hms_dec.current_nibble > 0) {
                            hms_dec.current_nibble = (hms_dec.current_nibble << 1) | bit;
                            if (++hms_dec.bit_cnt == 4) {
                                if (hms_dec.nibble_cnt < 24) hms_dec.nibbles[hms_dec.nibble_cnt++] = hms_dec.current_nibble;
                                hms_dec.current_nibble = 0; hms_dec.bit_cnt = 0;
                            }
                        }
                    }
                } else {
                    if (hms_dec.nibble_cnt > 0) reset_sensor(&hms_dec);
                }
                hms_dec.pulse_state = 0;
            }

            // --- Generic RTL_433 Sensor Style (OOK PWM) ---
            // Typical: Long High + Short Low = 1, Short High + Long Low = 0
            static uint16_t rtl_last_high = 0;
            if (level == 0) { // We just finished a HIGH pulse
                rtl_last_high = pulse;
            } else { // We just finished a LOW pulse
                if (rtl_last_high > 100 && pulse > 100) {
                    int bit = -1;
                    if (rtl_last_high > 600 && pulse < 600) bit = 1;
                    else if (rtl_last_high < 600 && pulse > 600) bit = 0;
                    
                    if (bit != -1) {
                        rtl_dec.bit_buffer = (rtl_dec.bit_buffer << 1) | bit;
                        if (rtl_dec.bit_cnt < 32) rtl_dec.bit_cnt++;
                    }
                }
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
    
    // Core 0 for real-time RF handling
    xTaskCreatePinnedToCore(slowrf_task, "slowrf_task", 8192, NULL, 10, NULL, 0);
    return ESP_OK;
}
