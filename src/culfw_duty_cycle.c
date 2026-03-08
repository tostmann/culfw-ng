#include "culfw_duty_cycle.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "DUTY_CYCLE";
static uint32_t airtime_ms_1h = 0; // Accumulated airtime in the last hour
static int64_t last_reset_time = 0;

void duty_cycle_init() {
    last_reset_time = esp_timer_get_time();
}

bool duty_cycle_add_tx(uint32_t duration_ms) {
    int64_t now = esp_timer_get_time();
    // 1 hour is 3600 * 1000 * 1000 us
    if (now - last_reset_time > 3600ULL * 1000 * 1000) {
        airtime_ms_1h = 0;
        last_reset_time = now;
    }

    // 1% rule: 36000 ms per hour max
    if (airtime_ms_1h + duration_ms > 36000) {
        ESP_LOGE(TAG, "Duty cycle exceeded! Blocking TX.");
        return false;
    }

    airtime_ms_1h += duration_ms;
    ESP_LOGI(TAG, "Current Airtime: %lu ms / 36000 ms", airtime_ms_1h);
    return true;
}

uint32_t duty_cycle_get_remaining() {
    return 36000 - airtime_ms_1h;
}
