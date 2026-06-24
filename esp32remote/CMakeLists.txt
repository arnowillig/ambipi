# ESP-IDF top-level project file
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Firmware version — single source of truth. Feeds the ESP-IDF app descriptor
# and the FW_VERSION macro (see main/CMakeLists.txt); shown in web UI + boot log.
set(PROJECT_VER "1.0.1")
project(esp32remote)
