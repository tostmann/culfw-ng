#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "cc1101.h"

static const char *TAG = "CUL32-C6";

void led_task(void *pvParameters) {
    gpio_reset_pin(GPIO_LED);
    gpio_set_direction(GPIO_LED, GPIO_MODE_OUTPUT);
    while (1) {
        gpio_set_level(GPIO_LED, 0); // Low active
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(GPIO_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(900));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting CUL32-C6 Firmware...");

    // Check 433MHz Marker
    gpio_reset_pin(GPIO_433MARKER);
    gpio_set_direction(GPIO_433MARKER, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_433MARKER, GPIO_PULLUP_ONLY);
    bool is_433 = (gpio_get_level(GPIO_433MARKER) == 0);
    ESP_LOGI(TAG, "Hardware detection: %s MHz", is_433 ? "433" : "868");

    // Initialize Radio
    if (cc1101_init() == ESP_OK) {
        ESP_LOGI(TAG, "CC1101 initialized successfully");
    } else {
        ESP_LOGE(TAG, "CC1101 initialization failed!");
    }

    xTaskCreate(led_task, "led_task", 2048, NULL, 5, NULL);
    
    #include "culfw_parser.h"
    xTaskCreate(culfw_parser_task, "culfw_parser_task", 4096, NULL, 10, NULL);
}
