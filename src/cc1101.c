#include "cc1101.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "CC1101";
static spi_device_handle_t spi;

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
        .clock_speed_hz = 1000000, // 1MHz
        .mode = 0,
        .spics_io_num = GPIO_SS,
        .queue_size = 7,
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) return ret;

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi);
    if (ret != ESP_OK) return ret;

    cc1101_cmd_strobe(CC1101_SRES);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t partnum = cc1101_read_reg(CC1101_PARTNUM | CC1101_READ_SINGLE);
    uint8_t version = cc1101_read_reg(CC1101_VERSION | CC1101_READ_SINGLE);

    ESP_LOGI(TAG, "CC1101 Partnum: 0x%02x, Version: 0x%02x", partnum, version);

    if (version == 0 || version == 0xFF) return ESP_FAIL;

    // Default configuration for SlowRF (OOK/ASK)
    gpio_set_direction(GPIO_433MARKER, GPIO_MODE_INPUT);
    bool is_433 = (gpio_get_level(GPIO_433MARKER) == 0);

    // Common SlowRF Setup (ASK, ~2.4k Baud, etc.)
    cc1101_write_reg(0x02, 0x0D); // IOCFG0: GDO0 Serial Data Output
    cc1101_write_reg(0x00, 0x2E); // IOCFG2: GDO2 High Impedance
    cc1101_write_reg(0x0B, 0x06); // FSCTRL1
    cc1101_write_reg(0x0C, 0x00); // FSCTRL0
    
    if (is_433) {
        cc1101_write_reg(0x0D, 0x10); // FREQ2
        cc1101_write_reg(0x0E, 0xB1); // FREQ1
        cc1101_write_reg(0x0F, 0x3B); // FREQ0
    } else {
        cc1101_write_reg(0x0D, 0x21); // FREQ2
        cc1101_write_reg(0x0E, 0x62); // FREQ1
        cc1101_write_reg(0x0F, 0x76); // FREQ0
    }
    
    cc1101_write_reg(0x10, 0xF8); // MDMCFG4
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
    cc1101_write_reg(0x22, 0x10); // FREND0
    cc1101_write_reg(0x23, 0xE9); // FSCAL3
    cc1101_write_reg(0x24, 0x2A); // FSCAL2
    cc1101_write_reg(0x25, 0x00); // FSCAL1
    cc1101_write_reg(0x26, 0x1F); // FSCAL0

    cc1101_write_reg(0x3E, 0xC0); // PATABLE: For ASK/OOK, first byte is the value for "1"

    cc1101_set_rx_mode();

    return ESP_OK;
}

void cc1101_set_rx_mode() {
    cc1101_cmd_strobe(CC1101_SIDLE);
    cc1101_write_reg(0x02, 0x0D); // IOCFG0: GDO0 Serial Data Output
    gpio_set_direction(GPIO_GDO0, GPIO_MODE_INPUT);
    cc1101_cmd_strobe(CC1101_SRX);
}

void cc1101_set_tx_mode() {
    cc1101_cmd_strobe(CC1101_SIDLE);
    cc1101_write_reg(0x02, 0x0D); // IOCFG0: GDO0 Serial Data Input (actually 0x0D is same pin but CC1101 will use it as input in TX)
    gpio_set_direction(GPIO_GDO0, GPIO_MODE_OUTPUT);
    cc1101_cmd_strobe(CC1101_STX);
}

void cc1101_set_idle_mode() {
    cc1101_cmd_strobe(CC1101_SIDLE);
}

esp_err_t cc1101_write_reg(uint8_t reg, uint8_t val) {
    spi_transaction_t t = {
        .length = 16,
        .tx_data = {reg, val},
        .flags = SPI_TRANS_USE_TXDATA
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
