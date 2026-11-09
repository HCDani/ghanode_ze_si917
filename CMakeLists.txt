# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ghanode)

target_sources(app PRIVATE
  src/main.c
  src/sensors.c
  src/wifi.c
  src/mqtt_link.c
  src/reading.c
  src/payload.c
  src/lux.c
  src/recovery.c
  src/sntp_sync.c
  src/timesync.c
)

# secret.h (gitignored) lives next to this file.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(CONFIG_GHANODE_MQTT_TLS)
  set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated)
  generate_inc_file_for_target(app ${CMAKE_CURRENT_SOURCE_DIR}/certs/isrg_root_x1.pem
                               ${gen_dir}/isrg_root_x1.pem.inc)
endif()
