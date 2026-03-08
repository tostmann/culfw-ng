#include "web_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "slowrf.h"
#include "cc1101.h"
#include "matter_bridge.h"
#include "generic_decoder.h"
#include "culfw_duty_cycle.h"
#include <string.h>
#include "esp_mac.h"
#include "wifi_manager.h"

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

    char *reg_dump = malloc(1024);
    if (reg_dump) cc1101_get_register_dump(reg_dump, 1024);

    bool is_433 = cc1101_is_433();
    uint8_t mode = slowrf_get_mode();

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[13];
    sprintf(mac_str, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    char ip_addr[16];
    wifi_manager_get_ip(ip_addr);

    snprintf(resp, 8192, 
        "<html><head><title>CUL32-C6 [%s]</title>"
        "<meta http-equiv='refresh' content='10'>"
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
        "<p><b>IP Address:</b> %s &nbsp; <b>MAC:</b> %s</p>"
        "<p><b>Frequency:</b> <span style='color: #e67e22;'>%s MHz</span> &nbsp; <a href='/cmd?c=f433'>Switch 433</a> <a href='/cmd?c=f868'>Switch 868</a></p>"
        "<p><b>RSSI (current):</b> <span style='color: #e67e22;'>%d</span></p>"
        "<p><b>Mode:</b> <span style='color: #e67e22;'>X%02X (%s)</span> &nbsp; <a href='/cmd?c=X21'>CUL Mode</a> <a href='/cmd?c=X25'>SIGduino Mode</a></p>"
        "<p><b>Duty Cycle (1h):</b> %lu / %lu ms used</p></div>"
        "<div class='card'>%s</div>"
        "<div class='card'><h3>CC1101 Register Map</h3><div class='log' style='background: #eee; color: #333; font-size: 0.8em;'>%s</div></div>"
        "<div class='card'>%s"
        "<h3>Matter Signal Injection (TX Simulation)</h3>"
        "<form action='/cmd' method='get' onsubmit='this.c.value=\"MC \" + this.c_ep.value + \" \" + this.c_val.value'>"
        "Endpoint ID: <input type='text' name='c_ep' size='3' value='10'> "
        "Value (0=Off, 1=On, or Float): <input type='text' name='c_val' size='3' value='1'> "
        "<button type='submit'>Send Command</button>"
        "<input type='hidden' name='c'>"
        "</form></div>"
        "<div class='card'><h3>Live Activity</h3><div class='log'>%s</div></div>"
        "<div class='card'><h3>Advanced Tools</h3>"
        "<h4>RF Injection (RX Simulation)</h4>"
        "<p>Format: Hex-Durations (10us units), 4 digits per pulse. e.g. 00320032 = 500us High, 500us Low</p>"
        "<form action='/cmd' method='get'>"
        "<input type='text' name='c' size='50' value='mi00320032'>"
        "<button type='submit'>Inject</button>"
        "</form>"
        "<h4>Factory Reset</h4>"
        "<p style='color: #e74c3c;'>Caution: This will erase all settings!</p>"
        "<a href='/cmd?c=e' style='background: #e74c3c;'>Wipe and Restart</a>"
        "</div>"
        "</body></html>",
        mac_str,
        BUILD_NUMBER,
        ip_addr,
        mac_str,
        is_433 ? "433" : "868",
        cc1101_read_rssi(),
        mode,
        mode == SLOWRF_MODE_CUL ? "CUL" : "SIGNALduino",
        (is_433 ? 360000 : 36000) - duty_cycle_get_remaining(),
        is_433 ? 360000 : 36000,
        proto_list,
        reg_dump ? reg_dump : "N/A",
        matter_list,
        events[0] ? events : "Waiting for radio signals..."
    );

    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    if (reg_dump) free(reg_dump);
    free(resp);
    free(matter_list);
    free(proto_list);
    free(events);
    return ESP_OK;
}

static esp_err_t cmd_get_handler(httpd_req_t *req) {
    char buf[128];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char cmd[64];
        if (httpd_query_key_value(buf, "c", cmd, sizeof(cmd)) == ESP_OK) {
            ESP_LOGI(TAG, "Web Command received: %s", cmd);
            extern void handle_command(char *cmd);
            handle_command(cmd);
        }
    }
    httpd_resp_set_status(req, "303 See Other");
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
    config.stack_size = 8192;

    ESP_LOGI(TAG, "Starting Web Server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &cmd_uri);
    }
}
