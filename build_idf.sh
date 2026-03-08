#!/bin/bash
export PATH=$HOME/.platformio/packages/tool-cmake/bin:$HOME/.platformio/packages/tool-ninja:$PATH
source $HOME/.platformio/packages/framework-espidf/export.sh
idf.py $@
