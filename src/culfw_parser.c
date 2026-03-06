#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "culfw_parser.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "cc1101.h"

static const char *TAG = "CULFW_PARSER";
static bool reporting_enabled = false;

bool culfw_reporting_enabled() {
    return reporting_enabled;
}

static void handle_command(char *cmd) {
    char out[128];
    int len = 0;
    if (cmd[0] == 'V') {
        bool is_433 = (gpio_get_level(GPIO_433MARKER) == 0);
        len = snprintf(out, sizeof(out), "V 1.0.1-NG CUL32-C6-%sMHz\r\n", is_433 ? "433" : "868");
    } else if (cmd[0] == 'X') {
        if (cmd[1] == '0' && cmd[2] == '0') {
            reporting_enabled = false;
        } else {
            reporting_enabled = true;
        }
        len = snprintf(out, sizeof(out), "X21\r\n");
    } else if (cmd[0] == 'C') {
        len = snprintf(out, sizeof(out), "C01\r\n");
    } else if (cmd[0] == 'F') {
        // F <hex_data>
        // cmd is like "F1122334455"
        cc1101_send_slowrf(cmd + 1);
        len = snprintf(out, sizeof(out), "F OK\r\n");
    } else {
        len = snprintf(out, sizeof(out), "E %s unknown\r\n", cmd);
    }
    
    if (len > 0) {
        usb_serial_jtag_write_bytes(out, len, portMAX_DELAY);
    }
}

void culfw_parser_task(void *pvParameters) {
    // Check if driver is already installed (might be by another task or earlier boot stage)
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t usb_serial_jtag_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        usb_serial_jtag_driver_install(&usb_serial_jtag_config);
    }

    uint8_t buf[128];
    char cmd_buf[128];
    int cmd_pos = 0;

    while (1) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(10));
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
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
