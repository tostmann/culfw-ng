#!/bin/bash
# Optimized build script for CULFW-NG supporting Build Profiles
# Syntax: ./build_idf.sh <command> [profile]
# Commands: build, flash, monitor, clean
# Profiles: wifi, thread, serial (default: wifi)

export PATH=$HOME/.platformio/packages/tool-cmake/bin:$HOME/.platformio/packages/tool-ninja:$PATH
export IDF_PATH=$HOME/.platformio/packages/framework-espidf
export IDF_TOOLS_PATH=$HOME/.espressif

# Check if environment is already sourced to speed up
if [ -z "$IDF_EXPORT_QUIET" ]; then
    export IDF_EXPORT_QUIET=1
    source $IDF_PATH/export.sh > /dev/null 2>&1
fi

CMD=$1
PROFILE=${2:-wifi}

if [ -z "$CMD" ]; then
    CMD="build"
fi

case "$CMD" in
    clean)
        echo "Cleaning build directory and sdkconfig..."
        rm -rf build sdkconfig sdkconfig.old dependencies.lock managed_components
        ;;
    build)
        echo "Building profile: $PROFILE"
        # We use a clean state for different profiles to avoid cache issues
        rm -rf sdkconfig dependencies.lock
        
        if [ "$PROFILE" == "serial" ]; then
            echo "Using Serial defaults..."
            idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults.serial" -DPROFILE_SERIAL=1 build
        elif [ "$PROFILE" == "thread" ]; then
            echo "Using Thread defaults..."
            # Merge base + thread defaults
            cat sdkconfig.defaults sdkconfig.defaults.thread > sdkconfig.combined
            idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.combined" build
            rm sdkconfig.combined
        else
            echo "Using WiFi defaults..."
            # Merge base + wifi defaults
            cat sdkconfig.defaults sdkconfig.defaults.wifi > sdkconfig.combined
            idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.combined" build
            rm sdkconfig.combined
        fi
        ;;
    flash|monitor)
        # Shift arguments to pass remaining to idf.py
        shift
        # If profile was provided, shift again
        if [[ "$1" =~ ^(wifi|thread|serial)$ ]]; then shift; fi
        idf.py $CMD "$@"
        ;;
    *)
        idf.py "$@"
        ;;
esac
