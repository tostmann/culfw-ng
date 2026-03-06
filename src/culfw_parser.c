#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "culfw_parser.h"
#include "esp_log.h"
#include "cc1101.h"

static const char *TAG = "CULFW_PARSER";

static void handle_command(char *cmd) {
    if (cmd[0] == 'V') {
        printf("V 1.0.0 CUL32-C6\n");
    } else if (cmd[0] == 'X') {
        printf("X01\n"); // Just as a placeholder for reporting
    } else if (cmd[0] == 'C') {
        // Radio config logic would go here
        printf("C00\n");
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
