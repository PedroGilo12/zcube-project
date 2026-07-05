# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zcube)

FILE(GLOB main_app_sources src/*.c)

list(REMOVE_ITEM main_app_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/bringup.c")

message("Files: " ${main_app_sources})

if(CONFIG_APP_FIRMWARE_BRINGUP)
  target_sources(app PRIVATE src/bringup.c)
else()
  target_sources(app PRIVATE ${main_app_sources})
endif()
