set(include_dirs "../include" "include")

# Define mandatory sources
set(app_sources
    main.c
    cc1101.c
    slowrf.c
    culfw_parser.c
    culfw_duty_cycle.c
    config_loader.c
    generic_decoder.c
)

# Optional components based on configuration
if (CONFIG_ESP_WIFI_ENABLED)
    message(STATUS "WiFi Enabled - adding WiFi sources")
    list(APPEND app_sources wifi_manager.c web_server.c)
endif()

if (CONFIG_OPENTHREAD_ENABLED)
    message(STATUS "Thread Enabled - adding Thread sources")
    list(APPEND app_sources thread_manager.c)
endif()

if (CONFIG_ESP_MATTER_ENABLE_WIFI OR CONFIG_ESP_MATTER_ENABLE_OPENTHREAD)
    message(STATUS "Matter Enabled - adding Matter sources")
    list(APPEND app_sources matter_bridge.c matter_interface.cpp)
endif()

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS ${include_dirs}
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES nvs_flash esp_partition spiffs driver esp_timer esp_netif esp_event esp_wifi esp_http_server json openthread esp_driver_usb_serial_jtag)
target_compile_definitions(${COMPONENT_LIB} PUBLIC 
    FW_VERSION="1.1.0-NG"
    BUILD_NUMBER=8
    GPIO_GDO0=2
    GPIO_GDO2=3
    GPIO_SS=18
    GPIO_SCK=19
    GPIO_MISO=20
    GPIO_MOSI=21
    GPIO_LED=8
    GPIO_SW=9
    GPIO_433MARKER=4
)
# Only enable Matter macro if WIFI or THREAD is enabled
if (CONFIG_ESP_WIFI_ENABLED OR CONFIG_OPENTHREAD_ENABLED)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CONFIG_ESP_MATTER_ENABLE=1)
endif()
