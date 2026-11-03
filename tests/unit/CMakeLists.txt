# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ghanode_unit)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_include_directories(app PRIVATE ${APP_SRC})
target_sources(app PRIVATE
  src/test_backoff.c
  src/test_lux.c
  src/test_payload.c
  src/test_reading.c
  src/test_recovery.c
  ${APP_SRC}/lux.c
  ${APP_SRC}/payload.c
  ${APP_SRC}/reading.c
  ${APP_SRC}/recovery.c
)
