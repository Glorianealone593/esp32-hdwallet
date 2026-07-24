# DibaVault — ESP32 HD Wallet Firmware
# Copyright (c) dibachain. All rights reserved.
#
# Portable across ESP32 / ESP32-S3 / ESP32-C3.
# Select the target with:  idf.py set-target esp32s3

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Embed the web UI assets into the firmware image (served from flash).
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

project(dibavault)

# Firmware version is injected from the git tag by CI (see .github/workflows).
# Falls back to a dev string for local builds.
if(NOT DEFINED DIBAVAULT_VERSION)
    set(DIBAVAULT_VERSION "0.1.0-dev")
endif()
add_compile_definitions(DIBAVAULT_VERSION="${DIBAVAULT_VERSION}")
