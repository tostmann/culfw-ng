#ifndef CC1101_H
#define CC1101_H

#include "esp_err.h"
#include "driver/spi_master.h"

esp_err_t cc1101_init();
esp_err_t cc1101_write_reg(uint8_t reg, uint8_t val);
esp_err_t cc1101_write_burst(uint8_t reg, const uint8_t *data, size_t len);
uint8_t cc1101_read_reg(uint8_t reg);
esp_err_t cc1101_cmd_strobe(uint8_t cmd);
void cc1101_set_rx_mode();
void cc1101_set_tx_mode();
void cc1101_set_idle_mode();
void cc1101_send_slowrf(const char* hex_data);

#define CC1101_WRITE_BURST  0x40
#define CC1101_READ_SINGLE  0x80
#define CC1101_READ_BURST   0xC0

// Status Registers
#define CC1101_PARTNUM      0x30
#define CC1101_VERSION      0x31

// Command Strobes
#define CC1101_SRES         0x30
#define CC1101_SFSTX        0x31
#define CC1101_SXOFF        0x32
#define CC1101_SCAL         0x33
#define CC1101_SRX          0x34
#define CC1101_STX          0x35
#define CC1101_SIDLE        0x36
#define CC1101_SWOR         0x38
#define CC1101_SPWD         0x39
#define CC1101_SFTX         0x3B
#define CC1101_SFRX         0x3C
#define CC1101_SNOP         0x3D

#endif
