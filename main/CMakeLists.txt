set(include_dirs "../include" "include")

file(GLOB_RECURSE app_sources *.c *.cpp)

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS ${include_dirs}
                       PRIV_INCLUDE_DIRS ".")
target_compile_definitions(${COMPONENT_LIB} PUBLIC 
    FW_VERSION="1.1.0-NG"
    BUILD_NUMBER=7
    GPIO_GDO0=2
    GPIO_GDO2=3
    GPIO_SS=18
    GPIO_SCK=19
    GPIO_MISO=20
    GPIO_MOSI=21
    GPIO_LED=8
    GPIO_SW=9
    GPIO_433MARKER=4
    CONFIG_ESP_MATTER_ENABLE=1
)
