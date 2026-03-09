#include "matter_interface.h"
#include "esp_log.h"
#include "driver/usb_serial_jtag.h"
#include <string.h>
#include <string>

static const char *TAG = "MATTER_IF";

static void log_jtag(const char* msg) {
    usb_serial_jtag_write_bytes(msg, strlen(msg), 0);
}
static matter_command_cb_t cmd_cb = NULL;

// If the SDK is available, include it here
#if defined(CONFIG_ESP_MATTER_ENABLE_DATA_MODEL)
    #include <esp_matter.h>
    #include <esp_matter_console.h>
    #include <esp_matter_ota.h>
    #include <setup_payload/SetupPayload.h>
    #include <setup_payload/QRCodeSetupPayloadGenerator.h>
    #include <setup_payload/ManualSetupPayloadGenerator.h>
    
    using namespace esp_matter;
    using namespace esp_matter::attribute;
    using namespace esp_matter::endpoint;

    static endpoint_t *s_aggregator_endpoint = nullptr;

    // SDK-specific static functions would go here
    static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg) {
        ESP_LOGI("MATTER_IF", "Matter Event: %d", event->Type);
    }

    static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data) {
        if (type == attribute::PRE_UPDATE) {
            // Log update - avoid b for float
            ESP_LOGI(TAG, "Attribute Update: EP %d Cluster %lX Attr %lX", endpoint_id, cluster_id, attribute_id);
            if (cluster_id == chip::app::Clusters::OnOff::Id && attribute_id == chip::app::Clusters::OnOff::Attributes::OnOff::Id) {
                 matter_interface_simulate_command(endpoint_id, val->val.b ? 1.0f : 0.0f);
            }
        }
        return ESP_OK;
    }

    static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id, uint8_t effect_variant, void *priv_data) {
        return ESP_OK;
    }
#endif

void matter_interface_init(void) {
#ifdef CONFIG_ESP_MATTER_ENABLE_DATA_MODEL
    log_jtag("MATTER_IF: Initializing Node...\n");
    
    node::config_t node_config;
    memset(&node_config, 0, sizeof(node_config));

    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    
    if (node) {
        endpoint::aggregator::config_t aggregator_config;
        s_aggregator_endpoint = endpoint::aggregator::create(node, &aggregator_config, ENDPOINT_FLAG_NONE, NULL);
        if (s_aggregator_endpoint) {
            log_jtag("MATTER_IF: Aggregator created.\n");
        } else {
            log_jtag("MATTER_IF: FAILED to create Aggregator!\n");
        }
    } else {
        log_jtag("MATTER_IF: FAILED to create Node!\n");
    }

    log_jtag("MATTER_IF: Starting ESP-Matter SDK...\n");
    esp_matter::start(app_event_cb);

    // Print actual commissioning information
    chip::PayloadContents payload;
    payload.version = 0;
    payload.discriminator.SetLongValue(3840);
    payload.setUpPINCode = 20202021;
    payload.vendorID = 0xFFF1;
    payload.productID = 0x8000;
    payload.commissioningFlow = chip::CommissioningFlow::kStandard;
    payload.rendezvousInformation.SetValue(chip::RendezvousInformationFlag::kOnNetwork);

    std::string manual_code;
    if (chip::ManualSetupPayloadGenerator(payload).payloadDecimalStringRepresentation(manual_code) == CHIP_NO_ERROR) {
        ESP_LOGW("MATTER_SETUP", "========================================");
        ESP_LOGW("MATTER_SETUP", "Manual Code: %s", manual_code.c_str());
        ESP_LOGW("MATTER_SETUP", "Passcode: %lu", payload.setUpPINCode);
        ESP_LOGW("MATTER_SETUP", "Discriminator: %d", payload.discriminator);
        ESP_LOGW("MATTER_SETUP", "========================================");
    }

#else
    ESP_LOGW(TAG, "Matter SDK NOT ENABLED. Running in simulation mode.");
#endif
}

uint16_t matter_interface_create_endpoint(const char* device_id, matter_device_type_t type) {
    uint16_t endpoint_id = 0xFFFF;

#ifdef CONFIG_ESP_MATTER_ENABLE_DATA_MODEL
    char log_buf[128];
    snprintf(log_buf, sizeof(log_buf), "MATTER_IF: Create EP for %s (type %d)...\n", device_id, type);
    log_jtag(log_buf);

    node_t *node = node::get();
    if (!node) {
        log_jtag("MATTER_IF: Node not initialized yet!\n");
        return 0xFFFF;
    }

    endpoint_t *endpoint = nullptr;
    endpoint_t *parent = s_aggregator_endpoint; 
    
    if (!parent) {
        log_jtag("MATTER_IF: Aggregator not found, using node as parent\n");
        parent = (endpoint_t*)node;
    }

    log_jtag("MATTER_IF: Locking Chip Stack...\n");
    esp_err_t l_res = lock::chip_stack_lock(portMAX_DELAY);
    log_jtag("MATTER_IF: Lock acquired, calling SDK create...\n");
    
    switch(type) {
        case DEVICE_TYPE_SWITCH: {
            on_off_light::config_t config;
            endpoint = on_off_light::create(parent, &config, ENDPOINT_FLAG_NONE, nullptr);
            break;
        }
        case DEVICE_TYPE_TEMP_SENSOR: {
            temperature_sensor::config_t config;
            endpoint = temperature_sensor::create(parent, &config, ENDPOINT_FLAG_NONE, nullptr);
            break;
        }
        case DEVICE_TYPE_OUTLET: {
            on_off_plugin_unit::config_t config;
            endpoint = on_off_plugin_unit::create(parent, &config, ENDPOINT_FLAG_NONE, nullptr);
            break;
        }
        case DEVICE_TYPE_COVER: {
            window_covering_device::config_t config;
            endpoint = window_covering_device::create(parent, &config, ENDPOINT_FLAG_NONE, nullptr);
            break;
        }
        default:
            log_jtag("MATTER_IF: Unknown device type!\n");
            lock::chip_stack_unlock();
            return 0xFFFF;
    }
    log_jtag("MATTER_IF: Unlocking Chip Stack...\n");
    lock::chip_stack_unlock();

    if (endpoint) {
        endpoint_id = endpoint::get_id(endpoint);
        snprintf(log_buf, sizeof(log_buf), "MATTER_IF: Created Matter Endpoint %d\n", endpoint_id);
        log_jtag(log_buf);
    } else {
        log_jtag("MATTER_IF: FAILED to create endpoint!\n");
    }
#else
    static uint16_t counter = 10;
    endpoint_id = counter++;
    ESP_LOGI(TAG, "[SIMULATION] Created Endpoint %d for device %s (Type: %d)", endpoint_id, device_id, type);
#endif

    return endpoint_id;
}

void matter_interface_update_attribute(uint16_t endpoint_id, float value) {
#if defined(CONFIG_ESP_MATTER_ENABLE_DATA_MODEL)
    esp_matter_attr_val_t attr_val;

    lock::chip_stack_lock(portMAX_DELAY);
    // 1. OnOff
    attr_val = esp_matter_bool(value > 0.5f);
    attribute::update(endpoint_id, chip::app::Clusters::OnOff::Id, chip::app::Clusters::OnOff::Attributes::OnOff::Id, &attr_val);

    // 2. Temperature (Value is in C, Matter expects 100 * C)
    attr_val = esp_matter_int16((int16_t)(value * 100));
    attribute::update(endpoint_id, chip::app::Clusters::TemperatureMeasurement::Id, chip::app::Clusters::TemperatureMeasurement::Attributes::MeasuredValue::Id, &attr_val);
    lock::chip_stack_unlock();

#else
    ESP_LOGI(TAG, "[SIMULATION] Updated Endpoint %d to value %.2f", endpoint_id, value);
#endif
}

const char* matter_interface_get_status(void) {
#ifdef CONFIG_ESP_MATTER_ENABLE_DATA_MODEL
    return "REAL";
#else
    return "SIMULATED";
#endif
}

void matter_interface_register_command_cb(matter_command_cb_t cb) {
    cmd_cb = cb;
}

// Internal: simulate receiving a command
void matter_interface_simulate_command(uint16_t endpoint_id, float value) {
    if (cmd_cb) {
        cmd_cb(endpoint_id, value);
    }
}
