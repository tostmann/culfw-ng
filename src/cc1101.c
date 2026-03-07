#include "cc1101.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "CC1101";
static spi_device_handle_t spi;
static bool cc1101_is_433_flag = false;

bool cc1101_is_433() {
    return cc1101_is_433_flag;
}

esp_err_t cc1101_init() {
    spi_bus_config_t buscfg = {
        .miso_io_num = GPIO_MISO,
        .mosi_io_num = GPIO_MOSI,
        .sclk_io_num = GPIO_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64
    };
    
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 500000, // 500kHz
        .mode = 0,
        .spics_io_num = GPIO_SS,
        .queue_size = 7,
    };

    ESP_LOGI(TAG, "Initializing SPI: SCK=%d, MISO=%d, MOSI=%d, SS=%d", GPIO_SCK, GPIO_MISO, GPIO_MOSI, GPIO_SS);
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) return ret;

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi);
    if (ret != ESP_OK) return ret;

    cc1101_cmd_strobe(CC1101_SRES);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Dummy read to clear anything
    cc1101_read_reg(CC1101_PARTNUM | CC1101_READ_BURST);
    
    uint8_t partnum = cc1101_read_reg(CC1101_PARTNUM | CC1101_READ_BURST);
    uint8_t version = cc1101_read_reg(CC1101_VERSION | CC1101_READ_BURST);

    ESP_LOGI(TAG, "CC1101 Partnum: 0x%02x, Version: 0x%02x", partnum, version);

    if (version == 0 || version == 0xFF) return ESP_FAIL;

    // Default configuration for SlowRF (OOK/ASK)
    gpio_config_t marker_conf = {
        .pin_bit_mask = (1ULL << GPIO_433MARKER),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&marker_conf);
    vTaskDelay(pdMS_TO_TICKS(10));
    cc1101_is_433_flag = (gpio_get_level(GPIO_433MARKER) == 0);
    ESP_LOGI(TAG, "Detected Frequency: %s MHz", cc1101_is_433_flag ? "433" : "868");
    bool is_433 = cc1101_is_433_flag;

    // Common SlowRF Setup (ASK, ~2.4k Baud, etc.)
    cc1101_write_reg(0x02, 0x0D); // IOCFG0: GDO0 Serial Data Output
    cc1101_write_reg(0x00, 0x0E); // IOCFG2: Carrier Sense (High when RSSI > Threshold)
    cc1101_write_reg(0x08, 0x32); // PKTCTRL0: Asynchronous Serial Mode
    cc1101_write_reg(0x0B, 0x06); // FSCTRL1
    cc1101_write_reg(0x0C, 0x00); // FSCTRL0
    
    if (is_433) {
        cc1101_write_reg(0x0D, 0x10); // FREQ2
        cc1101_write_reg(0x0E, 0xB3); // FREQ1
        cc1101_write_reg(0x0F, 0x3B); // FREQ0 (433.92 MHz)
    } else {
        cc1101_write_reg(0x0D, 0x21); // FREQ2
        cc1101_write_reg(0x0E, 0x65); // FREQ1
        cc1101_write_reg(0x0F, 0x6A); // FREQ0 (868.3 MHz)
    }
    
    cc1101_write_reg(0x10, 0x58); // MDMCFG4: BW 325kHz
    cc1101_write_reg(0x11, 0x93); // MDMCFG3
    cc1101_write_reg(0x12, 0x30); // MDMCFG2: ASK/OOK, No sync
    cc1101_write_reg(0x13, 0x22); // MDMCFG1
    cc1101_write_reg(0x14, 0xF8); // MDMCFG0
    cc1101_write_reg(0x15, 0x15); // DEVIATN
    cc1101_write_reg(0x18, 0x18); // MCSM0
    cc1101_write_reg(0x19, 0x16); // FOCCFG
    cc1101_write_reg(0x1A, 0x6C); // BSCFG
    cc1101_write_reg(0x1B, 0x03); // AGCCTRL2
    cc1101_write_reg(0x1C, 0x40); // AGCCTRL1
    cc1101_write_reg(0x1D, 0x91); // AGCCTRL0
    cc1101_write_reg(0x21, 0x56); // FREND1
    cc1101_write_reg(0x22, 0x11); // FREND0: ASK/OOK use PATABLE[1] for '1'
    cc1101_write_reg(0x23, 0xE9); // FSCAL3
    cc1101_write_reg(0x24, 0x2A); // FSCAL2
    cc1101_write_reg(0x25, 0x00); // FSCAL1
    cc1101_write_reg(0x26, 0x1F); // FSCAL0

    // PATABLE: [0]=0x00 (for '0'), [1]=0xC0 (for '1' -> +10dBm)
    uint8_t patable[8] = {0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    cc1101_write_burst(0x3E, patable, 8);
    

    cc1101_set_rx_mode();

    return ESP_OK;
}

void cc1101_set_rx_mode() {
    cc1101_cmd_strobe(CC1101_SIDLE);
    gpio_set_direction(GPIO_GDO0, GPIO_MODE_INPUT);
    cc1101_write_reg(0x02, 0x0D); // IOCFG0: GDO0 Serial Data Output
    cc1101_cmd_strobe(CC1101_SRX);
    gpio_intr_enable(GPIO_GDO0);
}

void cc1101_set_tx_mode() {
    gpio_intr_disable(GPIO_GDO0);
    cc1101_cmd_strobe(CC1101_SIDLE);
    vTaskDelay(pdMS_TO_TICKS(1));
    cc1101_write_reg(0x02, 0x0D); // IOCFG0: GDO0 Serial Data Input
    gpio_set_direction(GPIO_GDO0, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_GDO0, 0);
    cc1101_cmd_strobe(CC1101_STX);
    
    // Wait for TX mode
    int timeout = 100;
    while (((cc1101_read_reg(0x35 | CC1101_READ_BURST) & 0x70) != 0x30) && timeout--) {
        vTaskDelay(1);
    }
}

void cc1101_set_idle_mode() {
    cc1101_cmd_strobe(CC1101_SIDLE);
}

void cc1101_set_frequency(bool is_433) {
    cc1101_is_433_flag = is_433;
    
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_u8(my_handle, "freq", is_433 ? 43 : 86);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }

    cc1101_cmd_strobe(CC1101_SIDLE);
    if (is_433) {
        cc1101_write_reg(0x0D, 0x10); // FREQ2
        cc1101_write_reg(0x0E, 0xB3); // FREQ1
        cc1101_write_reg(0x0F, 0x3B); // FREQ0 (433.92 MHz)
    } else {
        cc1101_write_reg(0x0D, 0x21); // FREQ2
        cc1101_write_reg(0x0E, 0x65); // FREQ1
        cc1101_write_reg(0x0F, 0x6A); // FREQ0 (868.3 MHz)
    }
    cc1101_set_rx_mode();
}

uint8_t cc1101_read_rssi() {
    // RSSI is in 0x34 status register
    uint8_t rssi_raw = cc1101_read_reg(0x34 | CC1101_READ_BURST);
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
    // T = 420us
    // '0': T High, 3T Low, T High, 3T Low
    // '1': 3T High, T Low, 3T High, T Low
    // 'F': T High, 3T Low, 3T High, T Low
    if (bit == '0') {
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(420);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(1260);
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(420);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(1260);
    } else if (bit == '1') {
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(1260);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(420);
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(1260);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(420);
    } else { // 'F'
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(420);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(1260);
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(1260);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(420);
    }
}

void cc1101_send_it_v1(const char* data) {
    gpio_set_level(GPIO_LED, 0); // LED ON
    cc1101_set_tx_mode();
    vTaskDelay(pdMS_TO_TICKS(5));
    
    // Check MARCSTATE
    uint8_t marc = cc1101_read_reg(0x35 | CC1101_READ_BURST);
    if ((marc & 0x1F) != 0x13) { // 0x13 is TX
        // Try again
        cc1101_cmd_strobe(CC1101_SIDLE);
        cc1101_cmd_strobe(CC1101_STX);
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    for (int repeat = 0; repeat < 6; repeat++) {
        for (int i = 0; data[i]; i++) {
            it_v1_send_bit(data[i]);
        }
        // Sync/Gap: T high, 31T low
        gpio_set_level(GPIO_GDO0, 1); ets_delay_us(420);
        gpio_set_level(GPIO_GDO0, 0); ets_delay_us(13020);
    }
    cc1101_set_rx_mode();
}

void cc1101_send_raw_slowrf(const char* hex_data) {
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

esp_err_t cc1101_write_reg(uint8_t reg, uint8_t val) {
    spi_transaction_t t = {
        .length = 16,
        .tx_data = {reg, val},
        .flags = SPI_TRANS_USE_TXDATA
    };
    return spi_device_transmit(spi, &t);
}

esp_err_t cc1101_write_burst(uint8_t reg, const uint8_t *data, size_t len) {
    uint8_t tx_data[65];
    tx_data[0] = reg | CC1101_WRITE_BURST;
    memcpy(tx_data + 1, data, len);
    spi_transaction_t t = {
        .length = (len + 1) * 8,
        .tx_buffer = tx_data,
    };
    return spi_device_transmit(spi, &t);
}

uint8_t cc1101_read_reg(uint8_t reg) {
    uint8_t tx_data[2] = {reg | CC1101_READ_SINGLE, 0x00};
    uint8_t rx_data[2] = {0, 0};
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };
    spi_device_transmit(spi, &t);
    return rx_data[1];
}

esp_err_t cc1101_cmd_strobe(uint8_t cmd) {
    spi_transaction_t t = {
        .length = 8,
        .tx_data = {cmd},
        .flags = SPI_TRANS_USE_TXDATA
    };
    return spi_device_transmit(spi, &t);
}
