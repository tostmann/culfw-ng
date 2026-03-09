#!/bin/bash
# Optimized build script for CULFW-NG
# Use: ./build_idf.sh build
# Use: ./build_idf.sh flash
# Use: ./build_idf.sh monitor

export PATH=$HOME/.platformio/packages/tool-cmake/bin:$HOME/.platformio/packages/tool-ninja:$PATH
export IDF_PATH=$HOME/.platformio/packages/framework-espidf
export IDF_TOOLS_PATH=$HOME/.espressif

# Check if environment is already sourced to speed up
if [ -z "$IDF_EXPORT_QUIET" ]; then
    export IDF_EXPORT_QUIET=1
    source $IDF_PATH/export.sh > /dev/null 2>&1
fi

# Determine command and profile
# Syntax: ./build_idf.sh <command> [profile]
# Commands: build, flash, monitor
# Profiles: wifi, thread, serial (default: wifi)

CMD=$1
PROFILE=${2:-wifi}

if [ -z "$CMD" ]; then
    CMD="build"
fi

case "$CMD" in
    build)
        echo "Building profile: $PROFILE"
        rm -rf sdkconfig build
        if [ "$PROFILE" == "serial" ]; then
            cp "sdkconfig.defaults.$PROFILE" sdkconfig
            # Force re-evaluation of dependencies
            idf.py -DCONFIG_ESP_WIFI_ENABLED=n -DCONFIG_OPENTHREAD_ENABLED=n -DCONFIG_ESP_MATTER_ENABLE=n reconfigure
        else
            cat sdkconfig.defaults > sdkconfig
            if [ -f "sdkconfig.defaults.$PROFILE" ]; then
                cat "sdkconfig.defaults.$PROFILE" >> sdkconfig
            fi
            idf.py reconfigure
        fi
        idf.py build
        ;;
    flash|monitor)
        # Shift arguments to pass remaining to idf.py (like -p /dev/...)
        shift
        if [ "$CMD" == "monitor" ]; then shift; fi # Monitor doesn't need profile arg
        idf.py $CMD "$@"
        ;;
    *)
        idf.py "$@"
        ;;
esac

