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

    return ESP_OK;
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
