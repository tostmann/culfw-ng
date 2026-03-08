#include "culfw_duty_cycle.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "DUTY_CYCLE";
static uint32_t airtime_ms_1h = 0; // Accumulated airtime in the last hour
static int64_t last_reset_time = 0;

void duty_cycle_init() {
    last_reset_time = esp_timer_get_time();
}

#include "cc1101.h"

bool duty_cycle_add_tx(uint32_t duration_ms) {
    // Duty cycle only mandatory for 868 MHz in many regions (1%)
    // 433 MHz often has higher limits (10%) or LBT
    bool is_433 = cc1101_is_433();
    uint32_t limit = is_433 ? 360000 : 36000; // 10% vs 1%

    int64_t now = esp_timer_get_time();
    // 1 hour is 3600 * 1000 * 1000 us
    if (now - last_reset_time > 3600ULL * 1000 * 1000) {
        airtime_ms_1h = 0;
        last_reset_time = now;
    }

    if (airtime_ms_1h + duration_ms > limit) {
        ESP_LOGE(TAG, "Duty cycle (%s) exceeded! Blocking TX.", is_433 ? "10%" : "1%");
        return false;
    }

    airtime_ms_1h += duration_ms;
    ESP_LOGI(TAG, "Current Airtime: %lu ms / %lu ms", airtime_ms_1h, limit);
    return true;
}

uint32_t duty_cycle_get_remaining() {
    bool is_433 = cc1101_is_433();
    uint32_t limit = is_433 ? 360000 : 36000;
    return (airtime_ms_1h > limit) ? 0 : (limit - airtime_ms_1h);
}
