#include "cc1101.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "culfw_duty_cycle.h"

static const char *TAG = "CC1101";
static spi_device_handle_t spi;
static bool cc1101_is_433_flag = false;
static SemaphoreHandle_t spi_mutex = NULL;

void cc1101_lock() {
    if (spi_mutex) xSemaphoreTakeRecursive(spi_mutex, portMAX_DELAY);
}

void cc1101_unlock() {
    if (spi_mutex) xSemaphoreGiveRecursive(spi_mutex);
}

bool cc1101_is_433() {
    return cc1101_is_433_flag;
}

esp_err_t cc1101_init() {
    if (!spi_mutex) spi_mutex = xSemaphoreCreateRecursiveMutex();
    
    spi_bus_config_t buscfg = {
        .miso_io_num = GPIO_MISO,
        .mosi_io_num = GPIO_MOSI,
        .sclk_io_num = GPIO_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64
    };
    
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 500000, 
        .mode = 0,
        .spics_io_num = GPIO_SS,
        .queue_size = 7,
    };

    ESP_LOGI(TAG, "Initializing SPI...");
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) return ret;

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi);
    if (ret != ESP_OK) return ret;

    cc1101_lock();
    cc1101_cmd_strobe(CC1101_SRES);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    uint8_t version = cc1101_read_reg(CC1101_VERSION | CC1101_READ_BURST);
    cc1101_unlock();

    ESP_LOGI(TAG, "CC1101 Version: 0x%02x", version);
    if (version == 0 || version == 0xFF) return ESP_FAIL;

    gpio_config_t marker_conf = {
        .pin_bit_mask = (1ULL << GPIO_433MARKER),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&marker_conf);
    vTaskDelay(pdMS_TO_TICKS(10));
    cc1101_is_433_flag = (gpio_get_level(GPIO_433MARKER) == 0);

    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        uint8_t freq_val = 0;
        if (nvs_get_u8(my_handle, "freq", &freq_val) == ESP_OK) {
            if (freq_val == 43) cc1101_is_433_flag = true;
            else if (freq_val == 86) cc1101_is_433_flag = false;
        }
        nvs_close(my_handle);
    }

    cc1101_lock();
    cc1101_write_reg(0x02, 0x0E); // GDO2 = Carrier Sense
    cc1101_write_reg(0x00, 0x0D); // GDO0 = Serial Data Output (Async)
    cc1101_write_reg(0x08, 0x32); 
    cc1101_write_reg(0x0B, 0x06); 
    cc1101_write_reg(0x0C, 0x00); 
    
    if (cc1101_is_433_flag) {
        // 433.92 MHz
        // Freq = 433.92e6 / (26e6 / 2^16) = 1093745 = 0x10B071
        cc1101_write_reg(0x0D, 0x10); 
        cc1101_write_reg(0x0E, 0xB0); 
        cc1101_write_reg(0x0F, 0x71); 
    } else {
        cc1101_write_reg(0x0D, 0x21); 
        cc1101_write_reg(0x0E, 0x65); 
        cc1101_write_reg(0x0F, 0x6A); 
    }
    
    cc1101_write_reg(0x10, 0x57); // MDMCFG4: BW 325kHz (culfw)
    cc1101_write_reg(0x11, 0xC4); // MDMCFG3: DRATE (culfw)
    cc1101_write_reg(0x12, 0x30); // MDMCFG2: ASK/OOK, No preamble/sync
    cc1101_write_reg(0x13, 0x23); // MDMCFG1 (culfw)
    cc1101_write_reg(0x14, 0xB9); // MDMCFG0 (culfw)
    cc1101_write_reg(0x15, 0x15); // DEVIATN
    cc1101_write_reg(0x17, 0x30); // MCSM1: Always stay in RX after packet
    cc1101_write_reg(0x18, 0x18); // MCSM0
    cc1101_write_reg(0x19, 0x16); // FOCCFG
    cc1101_write_reg(0x1A, 0x6C); // BSCFG
    cc1101_write_reg(0x1B, 0x43); // AGCCTRL2: Optimized for OOK
    cc1101_write_reg(0x1C, 0x40); // AGCCTRL1: LNA Gain
    cc1101_write_reg(0x1D, 0x91); // AGCCTRL0
    cc1101_write_reg(0x21, 0x56); // FREND1
    cc1101_write_reg(0x22, 0x11); // FREND0
    cc1101_write_reg(0x23, 0xE9); // FSCAL3
    cc1101_write_reg(0x24, 0x2A); // FSCAL2
    cc1101_write_reg(0x25, 0x00); // FSCAL1
    cc1101_write_reg(0x26, 0x1F); // FSCAL0

    // Increase TX power: 0xC0 (~10dBm) -> 0xC6 (Max)
    uint8_t patable[8] = {0x00, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    cc1101_write_burst(0x3E, patable, 8);
    cc1101_unlock();

    cc1101_set_rx_mode();
    return ESP_OK;
}

void cc1101_set_rx_mode() {
    cc1101_lock();
    cc1101_cmd_strobe(CC1101_SIDLE);
    gpio_set_direction(GPIO_GDO0, GPIO_MODE_INPUT);
    cc1101_write_reg(0x02, 0x0E); // GDO2 = Carrier Sense
    cc1101_write_reg(0x00, 0x0D); // GDO0 = Serial Data (Async)
    cc1101_cmd_strobe(CC1101_SRX);
    gpio_intr_enable(GPIO_GDO0);
    cc1101_unlock();
}

void cc1101_set_tx_mode() {
    cc1101_lock();
    gpio_intr_disable(GPIO_GDO0);
    cc1101_cmd_strobe(CC1101_SIDLE);
    vTaskDelay(pdMS_TO_TICKS(1));
    cc1101_write_reg(0x02, 0x0E); // GDO2 = Carrier Sense
    cc1101_write_reg(0x00, 0x0D); // GDO0 = Serial Data
    gpio_set_direction(GPIO_GDO0, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_GDO0, 0);
    cc1101_cmd_strobe(CC1101_STX);
    
    int timeout = 100;
    // MARCSTATE TX is 0x13
    while (((cc1101_read_reg(0x35 | CC1101_READ_BURST) & 0x1F) != 0x13) && timeout--) {
        cc1101_unlock();
        vTaskDelay(pdMS_TO_TICKS(1));
        cc1101_lock();
    }
    cc1101_unlock();
}

void cc1101_set_idle_mode() {
    cc1101_lock();
    cc1101_cmd_strobe(CC1101_SIDLE);
    cc1101_unlock();
}

void cc1101_set_frequency_raw(uint32_t freq_hz) {
    // Freq = (F_osc / 2^16) * FREQ[23..0]
    // FREQ[23..0] = Freq * 2^16 / F_osc
    // F_osc = 26.000.000 Hz
    uint64_t freq_reg = ((uint64_t)freq_hz << 16) / 26000000;
    
    cc1101_lock();
    cc1101_cmd_strobe(CC1101_SIDLE);
    cc1101_write_reg(0x0D, (freq_reg >> 16) & 0xFF);
    cc1101_write_reg(0x0E, (freq_reg >> 8) & 0xFF);
    cc1101_write_reg(0x0F, freq_reg & 0xFF);
    cc1101_unlock();
}

void cc1101_set_frequency(bool is_433) {
    cc1101_is_433_flag = is_433;
    
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_u8(my_handle, "freq", is_433 ? 43 : 86);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }

    if (is_433) {
        cc1101_set_frequency_raw(433920000);
    } else {
        cc1101_set_frequency_raw(868300000);
    }
    cc1101_set_rx_mode();
}

uint8_t cc1101_read_rssi() {
    cc1101_lock();
    uint8_t rssi_raw = cc1101_read_reg(0x34 | CC1101_READ_BURST);
    cc1101_unlock();
    return rssi_raw;
}

#include "rom/ets_sys.h"

static void fs20_send_bit(int bit) {
    if (bit) {
        gpio_set_level(GPIO_GDO0, 1);
        ets_delay_us(600);
        gpio_set_level(GPIO_GDO0, 0);
        ets_delay_us(600);
    } else {
        gpio_set_level(GPIO_GDO0, 1);
        ets_delay_us(400);
        gpio_set_level(GPIO_GDO0, 0);
        ets_delay_us(400);
    }
}

static void it_v1_send_bit(char bit) {
    int T = 420; // Standard Intertechno timing
    if (bit == '0') {
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(T);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(T * 3);
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(T);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(T * 3);
    } else if (bit == '1') {
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(T * 3);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(T);
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(T * 3);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(T);
    } else { // 'F'
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(T);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(T * 3);
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(T * 3);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(T);
    }
}

void cc1101_send_it_v1(const char* data) {
    gpio_set_level(GPIO_LED, 0); // LED ON
    cc1101_set_tx_mode();
    vTaskDelay(pdMS_TO_TICKS(5));
    
    // Check MARCSTATE
    uint8_t marc = cc1101_read_reg(0x35 | CC1101_READ_BURST);
    if ((marc & 0x1F) != 0x13) { // 0x13 is TX
        ESP_LOGE(TAG, "TX failed to enter TX state (MARCSTATE: 0x%02X)", marc);
        cc1101_cmd_strobe(CC1101_SIDLE);
        cc1101_cmd_strobe(CC1101_STX);
        vTaskDelay(pdMS_TO_TICKS(2));
    } else {
        ESP_LOGI(TAG, "TX entered TX state successfully (MARCSTATE: 0x%02X)", marc);
    }

    int T = 420;
    for (int repeat = 0; repeat < 10; repeat++) {
        for (int i = 0; data[i]; i++) {
            it_v1_send_bit(data[i]);
        }
        // Sync/Gap: T high, 31T low
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(T);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(T * 31);
    }
    cc1101_set_rx_mode();
}

void cc1101_send_raw_slowrf(const char* hex_data) {
    int hex_len = strlen(hex_data);
    uint32_t estimated_ms = (25 + (hex_len / 2) * 9) * 10; // 10 repeats, approx 1ms per bit
    if (!duty_cycle_add_tx(estimated_ms)) return;

    gpio_set_level(GPIO_LED, 0); // LED ON
    cc1101_set_tx_mode();
    vTaskDelay(pdMS_TO_TICKS(5));

    for (int repeat = 0; repeat < 10; repeat++) {
        for(int i=0; i<24; i++) fs20_send_bit(0); 
        fs20_send_bit(1); 

        int hex_len = strlen(hex_data);
        for(int i=0; i<hex_len; i+=2) {
            char h[3] = {hex_data[i], hex_data[i+1], 0};
            uint8_t b = (uint8_t)strtol(h, NULL, 16);
            int parity = 0;
            for(int j=7; j>=0; j--) {
                int bit = (b >> j) & 1;
                fs20_send_bit(bit);
                if (bit) parity++;
            }
            fs20_send_bit(parity % 2);
        }
        fs20_send_bit(0);
        ets_delay_us(10000);
    }
    
    cc1101_set_rx_mode();
    gpio_set_level(GPIO_LED, 1); // LED OFF
}

void cc1101_send_fs20(const char* housecode, const char* addr, const char* cmd) {
    char hex[16];
    char tmp[3] = {0, 0, 0};
    
    tmp[0] = housecode[0]; tmp[1] = housecode[1];
    uint8_t hc1 = strtol(tmp, NULL, 16);
    
    tmp[0] = housecode[2]; tmp[1] = housecode[3];
    uint8_t hc2 = strtol(tmp, NULL, 16);
    
    uint8_t ad  = strtol(addr, NULL, 16);
    uint8_t cm  = strtol(cmd, NULL, 16);
    uint8_t cs  = (hc1 + hc2 + ad + cm + 6) & 0xFF;
    
    snprintf(hex, sizeof(hex), "%02X%02X%02X%02X%02X", hc1, hc2, ad, cm, cs);
    cc1101_send_raw_slowrf(hex);
}

void cc1101_send_it_v3(const char* data) {
    if (!duty_cycle_add_tx(200)) return;
    ESP_LOGI(TAG, "TX IT_V3: %s", data);
    // IT V3 (Self Learning) sending logic
    // data is a string of 32 bits ('0' or '1')
    gpio_set_level(GPIO_LED, 0);
    cc1101_set_tx_mode();
    vTaskDelay(pdMS_TO_TICKS(5));

    int T = 300; // Base timing 300us

    for (int repeat = 0; repeat < 6; repeat++) {
        // Sync: High T, Low 31T
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(T);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(T * 31);

        for (int i = 0; data[i]; i++) {
            if (data[i] == '0') {
                // Logical 0: (High T, Low 3T), (High T, Low 3T)
                gpio_set_level(GPIO_GDO0, 1); ets_delay_us(T);
                gpio_set_level(GPIO_GDO0, 0); ets_delay_us(T * 3);
                gpio_set_level(GPIO_GDO0, 1); ets_delay_us(T);
                gpio_set_level(GPIO_GDO0, 0); ets_delay_us(T * 3);
            } else {
                // Logical 1: (High T, Low 3T), (High 3T, Low T)
                gpio_set_level(GPIO_GDO0, 1); ets_delay_us(T);
                gpio_set_level(GPIO_GDO0, 0); ets_delay_us(T * 3);
                gpio_set_level(GPIO_GDO0, 1); ets_delay_us(T * 3);
                gpio_set_level(GPIO_GDO0, 0); ets_delay_us(T);
            }
        }
        // Final pulse to end frame
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(T);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(T * 31);
    }

    cc1101_set_rx_mode();
    gpio_set_level(GPIO_LED, 1);
}

void cc1101_send_hms(const char* hex_data) {
    gpio_set_level(GPIO_LED, 0);
    cc1101_set_tx_mode();
    vTaskDelay(pdMS_TO_TICKS(5));

    for (int repeat = 0; repeat < 3; repeat++) {
        // Preamble
        for(int i=0; i<16; i++) {
            gpio_set_level(GPIO_GDO0, 1); ets_delay_us(400);
            gpio_set_level(GPIO_GDO0, 0); ets_delay_us(400);
        }
        for(int i=0; hex_data[i]; i++) {
            uint8_t n = (hex_data[i] >= 'A') ? (hex_data[i]-'A'+10) : (hex_data[i]-'0');
            for(int b=3; b>=0; b--) {
                if((n >> b) & 1) {
                    gpio_set_level(GPIO_GDO0, 1); ets_delay_us(800);
                    gpio_set_level(GPIO_GDO0, 0); ets_delay_us(400);
                } else {
                    gpio_set_level(GPIO_GDO0, 1); ets_delay_us(400);
                    gpio_set_level(GPIO_GDO0, 0); ets_delay_us(400);
                }
            }
        }
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(10000);
    }
    cc1101_set_rx_mode();
    gpio_set_level(GPIO_LED, 1);
}

static void fht_bit(int bit) {
    if (bit) {
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(600);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(400);
    } else {
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(400);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(600);
    }
}

void cc1101_send_fht(const char* hex_data) {
    uint8_t data[16];
    int len = 0;
    for (int i = 0; i < (int)strlen(hex_data) && i < 32; i += 2) {
        char hex[3] = { hex_data[i], hex_data[i+1], 0 };
        data[len++] = strtol(hex, NULL, 16);
    }
    
    gpio_set_level(GPIO_LED, 0);
    cc1101_set_tx_mode();
    vTaskDelay(pdMS_TO_TICKS(5));

    for (int r = 0; r < 3; r++) {
        for (int i = 0; i < 12; i++) fht_bit(0);
        fht_bit(0); fht_bit(0); fht_bit(0); fht_bit(0);
        fht_bit(1); fht_bit(1); fht_bit(0); fht_bit(0);

        for (int i = 0; i < len; i++) {
            fht_bit(0); // Start bit
            for (int bit = 7; bit >= 0; bit--) fht_bit((data[i] >> bit) & 1);
            fht_bit(1); // Stop bit
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    gpio_set_level(GPIO_GDO0, 0);
    gpio_set_level(GPIO_LED, 1);
    cc1101_set_rx_mode();
}

static void os_manchester(int bit) {
    int T = 500;
    if (bit) {
        // '1': Transition High to Low mid-bit
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(T);
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(T);
    } else {
        // '0': Transition Low to High mid-bit
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(T);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(T);
    }
}

void cc1101_send_somfy(const char* hex_data) {
    if (!duty_cycle_add_tx(300)) return;
    uint8_t data[10];
    int len = 0;
    for (int i = 0; i < (int)strlen(hex_data) && i < 20; i += 2) {
        char hex[3] = { hex_data[i], hex_data[i+1], 0 };
        data[len++] = strtol(hex, NULL, 16);
    }

    gpio_set_level(GPIO_LED, 0);
    cc1101_set_frequency_raw(433420000);
    cc1101_set_tx_mode();
    vTaskDelay(pdMS_TO_TICKS(10));

    // 1. Wake-up pulse (only once)
    gpio_set_level(GPIO_GDO0, 1); ets_delay_us(9415);
    gpio_set_level(GPIO_GDO0, 0); ets_delay_us(89565);

    for (int r = 0; r < 3; r++) { // Repeat frame 3 times
        // 2. Hardware Sync (7 pulses)
        for(int i=0; i<7; i++) {
            gpio_set_level(GPIO_GDO0, 1); ets_delay_us(2500);
            gpio_set_level(GPIO_GDO0, 0); ets_delay_us(2500);
        }
        
        // 3. Software Sync
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(4550);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(640);

        // 4. Manchester Data (approx 1.2ms per bit)
        for (int i = 0; i < len; i++) {
            for (int bit = 7; bit >= 0; bit--) {
                if ((data[i] >> bit) & 1) {
                    // '1' is Low-to-High
                    gpio_set_level(GPIO_GDO0, 0); ets_delay_us(600);
                    gpio_set_level(GPIO_GDO0, 1); ets_delay_us(600);
                } else {
                    // '0' is High-to-Low
                    gpio_set_level(GPIO_GDO0, 1); ets_delay_us(600);
                    gpio_set_level(GPIO_GDO0, 0); ets_delay_us(600);
                }
            }
        }
        gpio_set_level(GPIO_GDO0, 0);
        vTaskDelay(pdMS_TO_TICKS(30)); // Inter-frame gap
    }

    cc1101_set_frequency(true); // Back to 433.92 (or whatever was configured)
    cc1101_set_rx_mode();
    gpio_set_level(GPIO_LED, 1);
}

void cc1101_send_oregon(const char* hex_data) {
    gpio_set_level(GPIO_LED, 0);
    cc1101_set_tx_mode();
    vTaskDelay(pdMS_TO_TICKS(5));

    for (int r = 0; r < 2; r++) {
        // Preamble: 16-24 '1' bits (short pulses)
        for (int i = 0; i < 32; i++) {
            gpio_set_level(GPIO_GDO0, 1); ets_delay_us(500);
            gpio_set_level(GPIO_GDO0, 0); ets_delay_us(500);
        }
        
        // Data in Manchester
        for (int i = 0; hex_data[i]; i++) {
            uint8_t n = (hex_data[i] >= 'A') ? (hex_data[i]-'A'+10) : (hex_data[i]-'0');
            for (int b = 0; b < 4; b++) { // OS sends LSB first
                os_manchester((n >> b) & 1);
            }
        }
        gpio_set_level(GPIO_GDO0, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    cc1101_set_rx_mode();
    gpio_set_level(GPIO_LED, 1);
}

esp_err_t cc1101_write_reg(uint8_t reg, uint8_t val) {
    cc1101_lock();
    spi_transaction_t t = {
        .length = 16,
        .tx_data = {reg, val},
        .flags = SPI_TRANS_USE_TXDATA
    };
    esp_err_t ret = spi_device_transmit(spi, &t);
    cc1101_unlock();
    return ret;
}

esp_err_t cc1101_write_burst(uint8_t reg, const uint8_t *data, size_t len) {
    cc1101_lock();
    uint8_t tx_data[65];
    tx_data[0] = reg | CC1101_WRITE_BURST;
    memcpy(tx_data + 1, data, len);
    spi_transaction_t t = {
        .length = (len + 1) * 8,
        .tx_buffer = tx_data,
    };
    esp_err_t ret = spi_device_transmit(spi, &t);
    cc1101_unlock();
    return ret;
}

uint8_t cc1101_read_reg(uint8_t reg) {
    cc1101_lock();
    uint8_t tx_data[2] = {reg | CC1101_READ_SINGLE, 0x00};
    uint8_t rx_data[2] = {0, 0};
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };
    spi_device_transmit(spi, &t);
    cc1101_unlock();
    return rx_data[1];
}

esp_err_t cc1101_cmd_strobe(uint8_t cmd) {
    cc1101_lock();
    spi_transaction_t t = {
        .length = 8,
        .tx_data = {cmd},
        .flags = SPI_TRANS_USE_TXDATA
    };
    esp_err_t ret = spi_device_transmit(spi, &t);
    cc1101_unlock();
    return ret;
}

void cc1101_get_register_dump(char* buf, size_t max_len) {
    int len = 0;
    cc1101_lock();
    for (int i = 0; i <= 0x2E; i++) {
        uint8_t val = cc1101_read_reg(i);
        len += snprintf(buf + len, max_len - len, "%02X:%02X ", i, val);
        if (i % 8 == 7) len += snprintf(buf + len, max_len - len, "\r\n");
        if (len > max_len - 20) break;
    }
    len += snprintf(buf + len, max_len - len, "\r\nStatus:\r\n");
    for (int i = 0x30; i <= 0x3D; i++) {
        uint8_t val = cc1101_read_reg(i | CC1101_READ_BURST);
        len += snprintf(buf + len, max_len - len, "%02X:%02X ", i, val);
        if (i % 8 == 7) len += snprintf(buf + len, max_len - len, "\r\n");
        if (len > max_len - 20) break;
    }
    cc1101_unlock();
}
