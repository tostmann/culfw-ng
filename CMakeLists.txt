cmake_minimum_required(VERSION 3.16.0)
# Force PlatformIO to recognize the managed components
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/managed_components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(CULFW-NG)
