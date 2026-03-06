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

static void save_reporting_state(bool state) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_u8(my_handle, "reporting", state ? 1 : 0);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}

static bool load_reporting_state() {
    nvs_handle_t my_handle;
    uint8_t state = 0;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        nvs_get_u8(my_handle, "reporting", &state);
        nvs_close(my_handle);
    }
    return state != 0;
}

bool culfw_reporting_enabled() {
    return reporting_enabled;
}

static void handle_command(char *cmd) {
    char out[128];
    int len = 0;
    if (cmd[0] == 'V') {
        bool is_433 = (gpio_get_level(GPIO_433MARKER) == 0);
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
    } else if (cmd[0] == 'F') {
        // F <housecode 4> <addr 2> <cmd 2>
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
    } else if (cmd[0] == 'T') {
        if (cmd[1] == 'r') {
            // Test Random: Send 5 random FS20 packets
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
        } else if (strlen(cmd) >= 7) {
            // T <housecode 4> <addr 2> -> T010101 (for compatibility with some systems)
            cc1101_send_raw_slowrf(cmd + 1);
            len = snprintf(out, sizeof(out), "T OK\r\n");
        }
        // Test Random: Send 5 random FS20 packets
        len = snprintf(out, sizeof(out), "Tr START\r\n");
        usb_serial_jtag_write_bytes(out, len, portMAX_DELAY);
        for (int i = 0; i < 5; i++) {
            char rnd_hex[11];
            uint32_t r = esp_random();
            // Just use the bytes directly for simplicity
            snprintf(rnd_hex, sizeof(rnd_hex), "%08X", r);
            
            char msg[64];
            int mlen = snprintf(msg, sizeof(msg), "TX: %s\r\n", rnd_hex);
            usb_serial_jtag_write_bytes(msg, mlen, portMAX_DELAY);

            cc1101_send_slowrf(rnd_hex);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        len = snprintf(out, sizeof(out), "Tr DONE\r\n");
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

    // Check if driver is already installed (might be by another task or earlier boot stage)
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
