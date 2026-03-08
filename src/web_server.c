#include "web_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "slowrf.h"
#include "cc1101.h"
#include "matter_bridge.h"
#include "generic_decoder.h"
#include <string.h>

static const char *TAG = "WEB_SERVER";

static esp_err_t index_get_handler(httpd_req_t *req) {
    char *events = malloc(2048);
    if (!events) return ESP_FAIL;
    slowrf_get_web_events(events, 2048);

    bool is_433 = cc1101_is_433();
    uint8_t mode = slowrf_get_mode();

    char *resp = malloc(4096);
    if (!resp) { free(events); return ESP_FAIL; }

    snprintf(resp, 4096, 
        "<html><head><title>CUL32-C6 Status</title></head><body>"
        "<h1>CUL32-C6 Gateway Status</h1>"
        "<p><b>Frequency:</b> %s MHz</p>"
        "<p><b>Mode:</b> X%02X (%s)</p>"
        "<h3>Last Events:</h3>"
        "<p>%s</p>"
        "<hr><p><small>CULFW-NG Build %d</small></p>"
        "</body></html>",
        is_433 ? "433" : "868",
        mode,
        mode == SLOWRF_MODE_CUL ? "CUL" : "SIGNALduino",
        events[0] ? events : "No events yet.",
        BUILD_NUMBER
    );

    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    free(resp);
    free(events);
    return ESP_OK;
}

static const httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_get_handler,
    .user_ctx  = NULL
};

void web_server_init(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192; // Increase stack size

    ESP_LOGI(TAG, "Starting Web Server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &index_uri);
    }
}
