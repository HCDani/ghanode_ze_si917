# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ghanode)

target_sources(app PRIVATE
  src/main.c
  src/sensors.c
  src/wifi.c
  src/mqtt_link.c
)

# secret.h (gitignored) lives next to this file.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
