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

# Determine command
CMD=$1
if [ -z "$CMD" ]; then
    CMD="build"
fi

# Run idf.py with the provided arguments, but skip 'clean' unless explicitly requested
# to avoid full recompiles on simple syntax errors.
idf.py "$@"

