# Minimal ESP-IDF project that link-tests the OpenH264 decoder component.
# Used by .github/workflows/esp32-idf-build.yml.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(openh264_esp32_smoke)
