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

# Our firmware uses some compact one-liners; keep IDF's strict warnings enabled
# but do not promote these purely-stylistic ones to hard errors.
idf_build_set_property(COMPILE_OPTIONS
    "-Wno-error=misleading-indentation;-Wno-error=unused-variable;-Wno-error=unused-but-set-variable;-Wno-error=unused-function;-Wno-error=format;-Wno-error=format-truncation;-Wno-error=format-overflow;-Wno-error=char-subscripts"
    APPEND)

# trezor-crypto and ESP-IDF's wpa_supplicant both provide a strong `hmac_sha256`
# (identical HMAC-SHA256 semantics). Allow the linker to pick the first — the
# standard resolution for using trezor-crypto alongside esp_wifi. Applied to the
# final executable so it reaches the actual link step.
target_link_options(${CMAKE_PROJECT_NAME}.elf PRIVATE "-Wl,--allow-multiple-definition")

# Firmware version is injected from the git tag by CI (see .github/workflows).
# Falls back to a dev string for local builds.
if(NOT DEFINED DIBAVAULT_VERSION)
    set(DIBAVAULT_VERSION "0.1.0-dev")
endif()
# idf_build_set_property propagates the define to every component (add_compile_
# definitions at the project root does not reach IDF component builds).
idf_build_set_property(COMPILE_DEFINITIONS "DIBAVAULT_VERSION=\"${DIBAVAULT_VERSION}\"" APPEND)
