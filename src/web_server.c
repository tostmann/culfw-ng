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
        "<style>body { font-family: -apple-system, system-ui, sans-serif; margin: 20px; background: #f4f7f6; color: #444; }"
        ".card { background: white; padding: 20px; border-radius: 12px; box-shadow: 0 4px 6px rgba(0,0,0,0.05); margin-bottom: 20px; }"
        "h1 { color: #2c3e50; font-weight: 300; } h3 { color: #34495e; border-bottom: 2px solid #3498db; padding-bottom: 10px; margin-top: 0; }"
        ".log { font-family: 'Courier New', Courier, monospace; background: #2b2b2b; color: #a9b7c6; padding: 15px; border-radius: 8px; max-height: 250px; overflow-y: auto; line-height: 1.4; }"
        "a { text-decoration: none; color: white; background: #3498db; padding: 3px 8px; border-radius: 4px; font-size: 0.9em; }"
        "a:hover { background: #2980b9; }"
        "ul { list-style: none; padding: 0; } li { padding: 8px 0; border-bottom: 1px solid #eee; }"
        "li:last-child { border: none; }"
        "input, button { padding: 5px; border-radius: 4px; border: 1px solid #ccc; }"
        "button { background: #2ecc71; color: white; border: none; cursor: pointer; }"
        "button:hover { background: #27ae60; }"
        "</style></head><body>"
        "<h1>CUL32-C6 Gateway <span style='font-size: 0.5em; vertical-align: middle;'>Build %d</span></h1>"
        "<div class='card'><h3>System Control</h3>"
        "<p><b>Frequency:</b> <span style='color: #e67e22;'>%s MHz</span> &nbsp; <a href='/cmd?c=f433'>Switch 433</a> <a href='/cmd?c=f868'>Switch 868</a></p>"
        "<p><b>Mode:</b> <span style='color: #e67e22;'>X%02X (%s)</span> &nbsp; <a href='/cmd?c=X21'>CUL Mode</a> <a href='/cmd?c=X25'>SIGduino Mode</a></p></div>"
        "<div class='card'>%s</div>"
        "<div class='card'>%s"
        "<h3>Matter Signal Injection (TX Simulation)</h3>"
        "<form action='/cmd' method='get' onsubmit='this.c.value=\"MC \" + this.c_ep.value + \" \" + this.c_val.value'>"
        "Endpoint ID: <input type='text' name='c_ep' size='3' value='10'> "
        "Value (0=Off, 1=On, or Float): <input type='text' name='c_val' size='3' value='1'> "
        "<button type='submit'>Send Command</button>"
        "<input type='hidden' name='c'>"
        "</form></div>"
        "<div class='card'><h3>Live Activity</h3><div class='log'>%s</div></div>"
        "</body></html>",
        BUILD_NUMBER,
        is_433 ? "433" : "868",
        mode,
        mode == SLOWRF_MODE_CUL ? "CUL" : "SIGNALduino",
        proto_list,
        matter_list,
        events[0] ? events : "Waiting for radio signals..."
    );
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
