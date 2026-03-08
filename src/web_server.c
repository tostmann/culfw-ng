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
    char *proto_list = malloc(2048);
    char *matter_list = malloc(2048);
    char *resp = malloc(8192);

    if (!events || !proto_list || !matter_list || !resp) {
        free(events); free(proto_list); free(matter_list); free(resp);
        return ESP_FAIL;
    }

    slowrf_get_web_events(events, 2048);
    generic_decoder_get_web_list(proto_list, 2048);
    matter_bridge_get_web_list(matter_list, 2048);

    bool is_433 = cc1101_is_433();
    uint8_t mode = slowrf_get_mode();

    snprintf(resp, 8192, 
        "<html><head><title>CUL32-C6 Status</title>"
        "<meta http-equiv='refresh' content='5'>"
        "<style>body { font-family: sans-serif; margin: 20px; background: #f0f2f5; }"
        ".card { background: white; padding: 15px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); margin-bottom: 20px; }"
        "h1 { color: #1a73e8; } h3 { border-bottom: 1px solid #ddd; padding-bottom: 5px; }"
        ".log { font-family: monospace; background: #333; color: #0f0; padding: 10px; border-radius: 4px; max-height: 300px; overflow-y: auto; }"
        "</style></head><body>"
        "<h1>CUL32-C6 Gateway Status</h1>"
        "<div class='card'><h3>System Info</h3>"
        "<p><b>Frequency:</b> %s MHz</p>"
        "<p><b>Mode:</b> X%02X (%s)</p></div>"
        "<div class='card'>%s</div>"
        "<div class='card'>%s</div>"
        "<div class='card'><h3>Live Events</h3><div class='log'>%s</div></div>"
        "<hr><p><small>CULFW-NG Build %d</small></p>"
        "</body></html>",
        is_433 ? "433" : "868",
        mode,
        mode == SLOWRF_MODE_CUL ? "CUL" : "SIGNALduino",
        proto_list,
        matter_list,
        events[0] ? events : "Waiting for radio signals...",
        BUILD_NUMBER
    );

    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    free(resp);
    free(matter_list);
    free(proto_list);
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
