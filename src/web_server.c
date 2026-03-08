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
        "<div class='card'><h3>System Control</h3>"
        "<p><b>Frequency:</b> %s MHz [<a href='/cmd?c=f433'>433</a>] [<a href='/cmd?c=f868'>868</a>]</p>"
        "<p><b>Mode:</b> X%02X (%s) [<a href='/cmd?c=X21'>X21 (CUL)</a>] [<a href='/cmd?c=X25'>X25 (SIG)</a>]</p></div>"
        "<div class='card'>%s</div>"
        "<div class='card'>%s"
        "<h3>Matter Simulation</h3>"
        "<form action='/cmd' method='get'>"
        "EP: <input type='text' name='c_ep' size='3' value='10'> "
        "Val: <input type='text' name='c_val' size='3' value='1'> "
        "<button type='submit' onclick='this.form.c.value=\"MC \" + this.form.c_ep.value + \" \" + this.form.c_val.value'>Simulate TX</button>"
        "<input type='hidden' name='c'>"
        "</form></div>"
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

static esp_err_t cmd_get_handler(httpd_req_t *req) {
    char buf[128];
    int ret, remaining = req->content_len;
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char cmd[64];
        if (httpd_query_key_value(buf, "c", cmd, sizeof(cmd)) == ESP_OK) {
            ESP_LOGI(TAG, "Web Command received: %s", cmd);
            // We use a helper function to avoid repeating the parser logic 
            // or just trigger handle_command if we expose it.
            // For now, let's simulate the command by injecting it into the parser's logic.
            extern void handle_command(char *cmd); // Declare extern
            handle_command(cmd);
        }
    }
    // Redirect back to index
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t cmd_uri = {
    .uri       = "/cmd",
    .method    = HTTP_GET,
    .handler   = cmd_get_handler,
    .user_ctx  = NULL
};

void web_server_init(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192; // Increase stack size

    ESP_LOGI(TAG, "Starting Web Server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &cmd_uri);
    }
}
