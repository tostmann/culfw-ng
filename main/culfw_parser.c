#include "main.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "culfw_parser.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_spiffs.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "cc1101.h"
#include "slowrf.h"
#include "matter_bridge.h"
#include "generic_decoder.h"
#include "config_loader.h"
#include "rom/ets_sys.h"
#include "esp_netif.h"
#include "culfw_duty_cycle.h"
#include "wifi_manager.h"

static const char *TAG = "CULFW_PARSER";
static bool reporting_enabled = false;

static void save_nvs_u8(const char* key, uint8_t val) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_u8(my_handle, key, val);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}

static uint8_t load_nvs_u8(const char* key, uint8_t default_val) {
    nvs_handle_t my_handle;
    uint8_t val = default_val;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        nvs_get_u8(my_handle, key, &val);
        nvs_close(my_handle);
    }
    return val;
}

static void save_reporting_state(bool state) {
    save_nvs_u8("reporting", state ? 1 : 0);
}

static bool load_reporting_state() {
    return load_nvs_u8("reporting", 0) != 0;
}

static void save_mode_state(uint8_t mode) {
    save_nvs_u8("mode", mode);
}

static uint8_t load_mode_state() {
    return load_nvs_u8("mode", SLOWRF_MODE_CUL);
}

bool culfw_reporting_enabled() {
    return reporting_enabled;
}

void handle_command(char *cmd) {
    char out[1024];
    int len = 0;
    if (cmd[0] == 'V') {
        bool is_433 = cc1101_is_433();
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        
        char ip_addr[16] = "0.0.0.0";
        char ip6_addr[40] = "::";
        const char* m_status = "N/A";
#ifndef PROFILE_SERIAL
        wifi_manager_get_ip(ip_addr);
        wifi_manager_get_ipv6(ip6_addr);
        m_status = matter_interface_get_status();
#endif

        uint8_t mode = slowrf_get_mode();
        uint32_t dc_rem = duty_cycle_get_remaining();
        len = snprintf(out, sizeof(out), "V %s culfw-NG Build: %d (%s %s) CUL32-C6 ID:%02X%02X%02X%02X%02X%02X IP:%s IP6:%s (F-Band: %sMHz) Mode:X%02X Matter:%s DC_Rem:%lums\r\n", 
                       FW_VERSION, BUILD_NUMBER, __DATE__, __TIME__, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ip_addr, ip6_addr, is_433 ? "433" : "868", mode, m_status, dc_rem);
    } else if (cmd[0] == 'X') {
        if (cmd[1] == '0' && cmd[2] == '0') {
            reporting_enabled = false;
            slowrf_set_debug(false);
            slowrf_set_reporting(false);
            len = snprintf(out, sizeof(out), "X00\r\n");
        } else if (cmd[1] == '9' && cmd[2] == '9') {
            slowrf_set_debug(true);
            reporting_enabled = true;
            slowrf_set_reporting(true);
            len = snprintf(out, sizeof(out), "X99\r\n");
        } else if (cmd[1] == '2' && cmd[2] == '1') {
            slowrf_set_mode(SLOWRF_MODE_CUL);
            reporting_enabled = true;
            slowrf_set_reporting(true);
            save_mode_state(SLOWRF_MODE_CUL);
            len = snprintf(out, sizeof(out), "X21\r\n");
        } else if (cmd[1] == '2' && cmd[2] == '5') {
            slowrf_set_mode(SLOWRF_MODE_SIGNALDUINO);
            reporting_enabled = true;
            slowrf_set_reporting(true);
            save_mode_state(SLOWRF_MODE_SIGNALDUINO);
            len = snprintf(out, sizeof(out), "X25\r\n");
        } else {
            // Default behavior for other X commands (like X? -> status)
            uint8_t m = slowrf_get_mode();
            len = snprintf(out, sizeof(out), "X%02X\r\n", reporting_enabled ? m : 0);
        }
        save_reporting_state(reporting_enabled);
    } else if (cmd[0] == 'C') {
        uint8_t part = cc1101_read_reg(CC1101_PARTNUM | CC1101_READ_BURST);
        uint8_t vers = cc1101_read_reg(CC1101_VERSION | CC1101_READ_BURST);
        uint8_t marc = cc1101_read_reg(0x35 | CC1101_READ_BURST);
        len = snprintf(out, sizeof(out), "C01 Part: 0x%02x, Vers: 0x%02x, MARC: 0x%02x\r\n", part, vers, marc);
    } else if (cmd[0] == 'R' && strlen(cmd) >= 3) {
        uint8_t reg = strtol(cmd + 1, NULL, 16);
        uint8_t val = cc1101_read_reg(reg | CC1101_READ_BURST);
        len = snprintf(out, sizeof(out), "R%02X%02X\r\n", reg, val);
    } else if (cmd[0] == 'W' && strlen(cmd) >= 5) {
        uint8_t reg = strtol((char[]){cmd[1], cmd[2], 0}, NULL, 16);
        uint8_t val = strtol(cmd + 3, NULL, 16);
        cc1101_write_reg(reg, val);
        len = snprintf(out, sizeof(out), "W%02X%02X\r\n", reg, val);
    } else if (cmd[0] == 'F') {
        if (strlen(cmd) == 9) {
            char hc[5], ad[3], cm[3];
            strncpy(hc, cmd + 1, 4); hc[4] = 0;
            strncpy(ad, cmd + 5, 2); ad[2] = 0;
            strncpy(cm, cmd + 7, 2); cm[2] = 0;
            cc1101_send_fs20(hc, ad, cm);
            len = snprintf(out, sizeof(out), "F OK\r\n");
        } else {
            cc1101_send_raw_slowrf(cmd + 1);
            len = snprintf(out, sizeof(out), "F OK (raw)\r\n");
        }
    } else if (cmd[0] == 'M' && cmd[1] == 'R' && cmd[2] == 'E' && cmd[3] == 'G') {
        char dump[1024];
        cc1101_get_register_dump(dump, sizeof(dump));
        // Remove <br> for serial output
        char *p = dump;
        while ((p = strstr(p, "<br>"))) {
            p[0] = ' '; p[1] = ' '; p[2] = ' '; p[3] = ' ';
            p += 4;
        }
        len = snprintf(out, sizeof(out), "REGS: %s\r\n", dump);
    } else if (cmd[0] == 'i' && cmd[1] == 's') {
        const char* is_data = cmd + 2;
        if (strlen(is_data) == 32) {
            cc1101_send_it_v3(is_data);
        } else {
            cc1101_send_it_v1(is_data);
        }
        len = snprintf(out, sizeof(out), "is OK\r\n");
    } else if (cmd[0] == 'H') {
        if (strlen(cmd) >= 11) {
            cc1101_send_hms(cmd + 1);
            len = snprintf(out, sizeof(out), "H OK\r\n");
        }
    } else if (cmd[0] == 'f') {
        if (strcmp(cmd + 1, "433") == 0) {
            cc1101_set_frequency(true);
            len = snprintf(out, sizeof(out), "f433 OK\r\n");
        } else if (strcmp(cmd + 1, "868") == 0) {
            cc1101_set_frequency(false);
            len = snprintf(out, sizeof(out), "f868 OK\r\n");
        }
    } else if (strcmp(cmd, "rssi") == 0) {
        uint8_t r = cc1101_read_rssi();
        len = snprintf(out, sizeof(out), "RSSI: %d\r\n", r);
    } else if (cmd[0] == 'e') { // Factory reset (erase NVS)
        nvs_flash_erase();
        len = snprintf(out, sizeof(out), "e OK - Restarting...\r\n");
        usb_serial_jtag_write_bytes(out, len, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else if (cmd[0] == 'l') { // LED control
        if (cmd[1] == '0' && cmd[2] == '1') {
            gpio_set_level(GPIO_LED, 0); // Low active
            len = snprintf(out, sizeof(out), "l01 OK\r\n");
        } else if (cmd[1] == '0' && cmd[2] == '0') {
            gpio_set_level(GPIO_LED, 1);
            len = snprintf(out, sizeof(out), "l00 OK\r\n");
        }
#if APP_MATTER_ENABLED == 1
    } else if (cmd[0] == 'M' && cmd[1] == 'T') { // MT <ID> <VAL> - Matter Test
        char mid[64];
        float mval = 0;
        ESP_LOGI(TAG, "MT Command: '%s'", cmd);
        if(sscanf(cmd + 2, "%s %f", mid, &mval) == 2) {
             matter_bridge_report_event(mid, "Test", DEVICE_TYPE_TEMP_SENSOR, mval);
             len = snprintf(out, sizeof(out), "MT OK: %s -> %.1f\r\n", mid, mval);
        } else {
             len = snprintf(out, sizeof(out), "MT ERR: %s\r\n", cmd);
        }
    } else if (cmd[0] == 'M' && cmd[1] == 'L') { // ML - Matter List
        matter_bridge_list_endpoints();
        len = snprintf(out, sizeof(out), "ML DONE\r\n");
    } else if (cmd[0] == 'M' && cmd[1] == 'A') { // MA <ID> <PROTO> <TYPE> - Matter Add (Manual)
        char mid[64], mproto[16];
        int mtype;
        if(sscanf(cmd + 2, "%s %s %d", mid, mproto, &mtype) == 3) {
             matter_bridge_report_event(mid, mproto, (matter_device_type_t)mtype, 0.0);
             len = snprintf(out, sizeof(out), "MA OK: %s added as %s (Type %d)\r\n", mid, mproto, mtype);
        } else {
             len = snprintf(out, sizeof(out), "MA ERR: Use MA <ID> <PROTO> <TYPE>\r\n");
        }
    } else if (cmd[0] == 'M' && cmd[1] == 'C') { // MC <EP> <VAL> - Matter Command Simulation
        uint16_t ep;
        float val;
        if (sscanf(cmd + 3, "%hu %f", &ep, &val) == 2) {
            matter_interface_simulate_command(ep, val);
            len = snprintf(out, sizeof(out), "MC OK\r\n");
        } else {
            len = snprintf(out, sizeof(out), "MC ERR\r\n");
        }
#endif
    } else if (cmd[0] == 'G' && cmd[1] == 'L') { // GL - Generic List
        generic_decoder_list_protocols();
        len = snprintf(out, sizeof(out), "GL DONE\r\n");
    } else if (cmd[0] == 'G' && cmd[1] == 'R') { // GR - Generic Reload
        if (config_loader_load_protocols()) {
            len = snprintf(out, sizeof(out), "GR OK\r\n");
        } else {
            len = snprintf(out, sizeof(out), "GR ERR\r\n");
        }
    } else if (cmd[0] == 'm' && cmd[1] == 'i') { // mi<HEX4> - inject raw durations for testing (RX path)
        char hex[5];
        hex[4] = 0;
        uint8_t level = 0; // First pulse results in pulse_level 1 (HIGH)
        int clen = (int)strlen(cmd);
        printf("mi cmd len: %d\r\n", clen);
        for (int i = 2; i <= clen - 4; i += 4) {
            strncpy(hex, cmd + i, 4);
            uint32_t duration = strtol(hex, NULL, 16) * 10;
            if (duration > 0) {
                // ESP_LOGI(TAG, "mi pulse: %d level: %d", (int)duration, level);
                slowrf_process_pulse((uint16_t)duration, level);
                level = !level;
            }
        }
        len = snprintf(out, sizeof(out), "mi OK\r\n");
    } else if (cmd[0] == 'm') { // m<HEX> - send raw durations (dur = hex * 10us)
        cc1101_set_tx_mode();
        gpio_set_level(GPIO_LED, 0);
        for (int i = 1; cmd[i] && cmd[i+1]; i += 2) {
            char hex_str[3] = {cmd[i], cmd[i+1], 0};
            int dur = strtol(hex_str, NULL, 16) * 10;
            gpio_set_level(GPIO_GDO0, ((i-1)/2) % 2 == 0); // Toggle level
            ets_delay_us(dur);
        }
        gpio_set_level(GPIO_GDO0, 0);
        gpio_set_level(GPIO_LED, 1);
        cc1101_set_rx_mode();
        len = snprintf(out, sizeof(out), "m OK\r\n");
    } else if (cmd[0] == 'Y') {
        if (cmd[1] == 's') { // Ys<HEX> - Somfy
            cc1101_send_somfy(cmd + 2);
            len = snprintf(out, sizeof(out), "Ys OK\r\n");
        }
    } else if (cmd[0] == 'T') {
        if (cmd[1] == 'r') {
            len = snprintf(out, sizeof(out), "Tr START\r\n");
            usb_serial_jtag_write_bytes(out, len, portMAX_DELAY);
            for (int i = 0; i < 5; i++) {
                char rnd_hex[11];
                uint32_t r = esp_random();
                snprintf(rnd_hex, sizeof(rnd_hex), "%08X", r);
                char msg[64];
                int mlen = snprintf(msg, sizeof(msg), "TX: %s\r\n", rnd_hex);
                usb_serial_jtag_write_bytes(msg, mlen, portMAX_DELAY);
                cc1101_send_raw_slowrf(rnd_hex);
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            len = snprintf(out, sizeof(out), "Tr DONE\r\n");
        } else if (cmd[1] == 'X') {
            if (cmd[2] == '1') {
                cc1101_set_tx_mode();
                gpio_set_level(GPIO_GDO0, 1);
                len = snprintf(out, sizeof(out), "TX ON\r\n");
            } else {
                cc1101_set_rx_mode();
                len = snprintf(out, sizeof(out), "TX OFF\r\n");
            }
        } else if (cmd[1] == 'o') { // To<HEX> - Test Oregon
            cc1101_send_oregon(cmd + 2);
            len = snprintf(out, sizeof(out), "To OK\r\n");
        } else if (strlen(cmd) >= 11) {
            cc1101_send_fht(cmd + 1);
            len = snprintf(out, sizeof(out), "T OK\r\n");
        } else if (strlen(cmd) >= 7) {
            cc1101_send_raw_slowrf(cmd + 1);
            len = snprintf(out, sizeof(out), "T OK\r\n");
        }
    } else {
        len = snprintf(out, sizeof(out), "E %s unknown\r\n", cmd);
    }
    
    if (len > 0) {
        usb_serial_jtag_write_bytes(out, len, portMAX_DELAY);
    }
}

static char cmd_buf[2048];

void culfw_parser_task(void *pvParameters) {
    reporting_enabled = load_reporting_state();
    slowrf_set_reporting(reporting_enabled);
    ESP_LOGI(TAG, "Loaded reporting state: %d", reporting_enabled);

    // Set loaded mode
    uint8_t mode = load_mode_state();
    slowrf_set_mode(mode);
    ESP_LOGI(TAG, "Loaded RF mode: X%02X", mode);

    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t usb_serial_jtag_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        usb_serial_jtag_config.rx_buffer_size = 1024; // Increase RX buffer to handle long 'mi' commands
        usb_serial_jtag_driver_install(&usb_serial_jtag_config);
    }

    uint8_t buf[256];
    int cmd_pos = 0;

    while (1) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(10));
        if (n > 0) {
            // ESP_LOGI(TAG, "Received %d bytes", n);
            for (int i = 0; i < n; i++) {
                char c = (char)buf[i];
                if (c == '\r' || c == '\n') {
                    if (cmd_pos > 0) {
                        cmd_buf[cmd_pos] = '\0';
                        ESP_LOGI(TAG, "Executing command: %s", cmd_buf);
                        handle_command(cmd_buf);
                        cmd_pos = 0;
                    }
                } else if (cmd_pos < sizeof(cmd_buf) - 1) {
                    cmd_buf[cmd_pos++] = c;
                } else {
                    ESP_LOGE(TAG, "Command buffer overflow!");
                    cmd_pos = 0;
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
