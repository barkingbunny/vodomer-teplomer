# Vodomer-teplomer - FW pro M5Stack Basic (ESP32), ESP-IDF 6.1.
# Build: tools/build.ps1 (build adresar mimo Google Drive), viz CLAUDE.md.
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Prelozit jen komponenty, ktere main skutecne potrebuje.
idf_build_set_property(MINIMAL_BUILD ON)
project(vodomer-teplomer)
