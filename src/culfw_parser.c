#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "culfw_parser.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "cc1101.h"
#include "slowrf.h"

static const char *TAG = "CULFW_PARSER";
static bool reporting_enabled = false;

static void save_nvs_u8(const char* key, uint8_t val) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_u8(my_handle, key, val);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}

static uint8_t load_nvs_u8(const char* key, uint8_t default_val) {
    nvs_handle_t my_handle;
    uint8_t val = default_val;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        nvs_get_u8(my_handle, key, &val);
        nvs_close(my_handle);
    }
    return val;
}

static void save_reporting_state(bool state) {
    save_nvs_u8("reporting", state ? 1 : 0);
}

static bool load_reporting_state() {
    return load_nvs_u8("reporting", 0) != 0;
}

bool culfw_reporting_enabled() {
    return reporting_enabled;
}

static void handle_command(char *cmd) {
    char out[128];
    int len = 0;
    if (cmd[0] == 'V') {
        bool is_433 = cc1101_is_433();
        len = snprintf(out, sizeof(out), "V %s culfw-NG Build: %d (%s %s) CUL32-C6 (F-Band: %sMHz)\r\n", 
                       FW_VERSION, BUILD_NUMBER, __DATE__, __TIME__, is_433 ? "433" : "868");
    } else if (cmd[0] == 'X') {
        if (cmd[1] == '0' && cmd[2] == '0') {
            reporting_enabled = false;
            slowrf_set_debug(false);
            slowrf_set_reporting(false);
        } else if (cmd[1] == '9' && cmd[2] == '9') {
            slowrf_set_debug(true);
            reporting_enabled = true;
            slowrf_set_reporting(true);
        } else {
            reporting_enabled = true;
            slowrf_set_debug(false);
            slowrf_set_reporting(true);
        }
        save_reporting_state(reporting_enabled);
        len = snprintf(out, sizeof(out), "X%02d\r\n", reporting_enabled ? 21 : 0);
    } else if (cmd[0] == 'C') {
        uint8_t part = cc1101_read_reg(CC1101_PARTNUM | CC1101_READ_BURST);
        uint8_t vers = cc1101_read_reg(CC1101_VERSION | CC1101_READ_BURST);
        uint8_t marc = cc1101_read_reg(0x35 | CC1101_READ_BURST);
        len = snprintf(out, sizeof(out), "C01 Part: 0x%02x, Vers: 0x%02x, MARC: 0x%02x\r\n", part, vers, marc);
    } else if (cmd[0] == 'R' && strlen(cmd) >= 3) {
        uint8_t reg = strtol(cmd + 1, NULL, 16);
        uint8_t val = cc1101_read_reg(reg | CC1101_READ_BURST);
        len = snprintf(out, sizeof(out), "R%02X%02X\r\n", reg, val);
    } else if (cmd[0] == 'W' && strlen(cmd) >= 5) {
        uint8_t reg = strtol((char[]){cmd[1], cmd[2], 0}, NULL, 16);
        uint8_t val = strtol(cmd + 3, NULL, 16);
        cc1101_write_reg(reg, val);
        len = snprintf(out, sizeof(out), "W%02X%02X\r\n", reg, val);
    } else if (cmd[0] == 'F') {
        if (strlen(cmd) == 9) {
            char hc[5], ad[3], cm[3];
            strncpy(hc, cmd + 1, 4); hc[4] = 0;
            strncpy(ad, cmd + 5, 2); ad[2] = 0;
            strncpy(cm, cmd + 7, 2); cm[2] = 0;
            cc1101_send_fs20(hc, ad, cm);
            len = snprintf(out, sizeof(out), "F OK\r\n");
        } else {
            cc1101_send_raw_slowrf(cmd + 1);
            len = snprintf(out, sizeof(out), "F OK (raw)\r\n");
        }
    } else if (cmd[0] == 'i' && cmd[1] == 's') {
        const char* is_data = cmd + 2;
        if (strlen(is_data) == 32) {
            cc1101_send_it_v3(is_data);
        } else {
            cc1101_send_it_v1(is_data);
        }
        len = snprintf(out, sizeof(out), "is OK\r\n");
    } else if (cmd[0] == 'H') {
        if (strlen(cmd) >= 11) {
            cc1101_send_hms(cmd + 1);
            len = snprintf(out, sizeof(out), "H OK\r\n");
        }
    } else if (cmd[0] == 'f') {
        if (strcmp(cmd + 1, "433") == 0) {
            cc1101_set_frequency(true);
            len = snprintf(out, sizeof(out), "f433 OK\r\n");
        } else if (strcmp(cmd + 1, "868") == 0) {
            cc1101_set_frequency(false);
            len = snprintf(out, sizeof(out), "f868 OK\r\n");
        }
    } else if (cmd[0] == 'm') { // m<HEX> - send raw durations (dur = hex * 10us)
        cc1101_set_tx_mode();
        gpio_set_level(GPIO_LED, 0);
        for (int i = 1; cmd[i] && cmd[i+1]; i += 2) {
            char hex_str[3] = {cmd[i], cmd[i+1], 0};
            int dur = strtol(hex_str, NULL, 16) * 10;
            gpio_set_level(GPIO_GDO0, ((i-1)/2) % 2 == 0); // Toggle level
            ets_delay_us(dur);
        }
        gpio_set_level(GPIO_GDO0, 0);
        gpio_set_level(GPIO_LED, 1);
        cc1101_set_rx_mode();
        len = snprintf(out, sizeof(out), "m OK\r\n");
    } else if (cmd[0] == 'T') {
        if (cmd[1] == 'r') {
            len = snprintf(out, sizeof(out), "Tr START\r\n");
            usb_serial_jtag_write_bytes(out, len, portMAX_DELAY);
            for (int i = 0; i < 5; i++) {
                char rnd_hex[11];
                uint32_t r = esp_random();
                snprintf(rnd_hex, sizeof(rnd_hex), "%08X", r);
                char msg[64];
                int mlen = snprintf(msg, sizeof(msg), "TX: %s\r\n", rnd_hex);
                usb_serial_jtag_write_bytes(msg, mlen, portMAX_DELAY);
                cc1101_send_raw_slowrf(rnd_hex);
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            len = snprintf(out, sizeof(out), "Tr DONE\r\n");
        } else if (cmd[1] == 'X') {
            if (cmd[2] == '1') {
                cc1101_set_tx_mode();
                gpio_set_level(GPIO_GDO0, 1);
                len = snprintf(out, sizeof(out), "TX ON\r\n");
            } else {
                cc1101_set_rx_mode();
                len = snprintf(out, sizeof(out), "TX OFF\r\n");
            }
        } else if (cmd[1] == 'o') { // To<HEX> - Test Oregon
            cc1101_send_oregon(cmd + 2);
            len = snprintf(out, sizeof(out), "To OK\r\n");
        } else if (strlen(cmd) >= 11) {
            cc1101_send_fht(cmd + 1);
            len = snprintf(out, sizeof(out), "T OK\r\n");
        } else if (strlen(cmd) >= 7) {
            cc1101_send_raw_slowrf(cmd + 1);
            len = snprintf(out, sizeof(out), "T OK\r\n");
        }
    } else {
        len = snprintf(out, sizeof(out), "E %s unknown\r\n", cmd);
    }
    
    if (len > 0) {
        usb_serial_jtag_write_bytes(out, len, portMAX_DELAY);
    }
}

void culfw_parser_task(void *pvParameters) {
    reporting_enabled = load_reporting_state();
    slowrf_set_reporting(reporting_enabled);
    ESP_LOGI(TAG, "Loaded reporting state: %d", reporting_enabled);

    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t usb_serial_jtag_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        usb_serial_jtag_driver_install(&usb_serial_jtag_config);
    }

    uint8_t buf[128];
    char cmd_buf[128];
    int cmd_pos = 0;

    while (1) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(50));
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                char c = (char)buf[i];
                if (c == '\r' || c == '\n') {
                    if (cmd_pos > 0) {
                        cmd_buf[cmd_pos] = '\0';
                        handle_command(cmd_buf);
                        cmd_pos = 0;
                    }
                } else if (cmd_pos < sizeof(cmd_buf) - 1) {
                    cmd_buf[cmd_pos++] = c;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
