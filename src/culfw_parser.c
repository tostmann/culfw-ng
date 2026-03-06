#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "culfw_parser.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "cc1101.h"

static const char *TAG = "CULFW_PARSER";

static void handle_command(char *cmd) {
    if (cmd[0] == 'V') {
        gpio_set_direction(GPIO_433MARKER, GPIO_MODE_INPUT);
        bool is_433 = (gpio_get_level(GPIO_433MARKER) == 0);
        printf("V 1.0.1-NG CUL32-C6-%sMHz\r\n", is_433 ? "433" : "868");
    } else if (cmd[0] == 'X') {
        // X0x, X21, etc.
        printf("X21\r\n");
    } else if (cmd[0] == 'C') {
        // Cxx settings
        printf("C01\r\n");
    } else if (cmd[0] == 'F') {
        // FS20 / FHT / HMS etc.
        // Send: F <addr> <cmd> <ext>
        // Receive: F <hex_data>
        ESP_LOGI(TAG, "SlowRF command: %s", cmd);
    } else {
        ESP_LOGI(TAG, "Unknown command: %s", cmd);
    }
}

void culfw_parser_task(void *pvParameters) {
    char buf[128];
    int pos = 0;
    while (1) {
        int c = getchar();
        if (c != EOF) {
            if (c == '\r' || c == '\n') {
                if (pos > 0) {
                    buf[pos] = '\0';
                    handle_command(buf);
                    pos = 0;
                }
            } else if (pos < sizeof(buf) - 1) {
                buf[pos++] = (char)c;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
