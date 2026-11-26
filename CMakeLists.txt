# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hci_uart)

target_sources(app PRIVATE src/main.c)

if(CONFIG_BOARD_NRF52840DONGLE_NRF52840 AND CONFIG_BOARD_HAS_NRF5_BOOTLOADER)
  set(FIRMWARE_VERSION "" CACHE STRING "Override the dongle firmware's semantic version")
  set(dfu_version "${FIRMWARE_VERSION}")
  if(dfu_version STREQUAL "")
    if("$ENV{GITHUB_REF_TYPE}" STREQUAL "tag")
      set(dfu_version "$ENV{GITHUB_REF_NAME}")
    else()
      set(dfu_version "0.0.0-dev")
    endif()
  endif()

  string(REGEX REPLACE "^v" "" dfu_version "${dfu_version}")
  if(NOT dfu_version MATCHES "^(0|[1-9][0-9]*)[.](0|[1-9][0-9]*)[.](0|[1-9][0-9]*)(-[0-9A-Za-z-]+([.][0-9A-Za-z-]+)*)?([+][0-9A-Za-z-]+([.][0-9A-Za-z-]+)*)?$")
    message(FATAL_ERROR "Invalid dongle firmware version: ${dfu_version}")
  endif()

  set(dfu_version_major "${CMAKE_MATCH_1}")
  set(dfu_version_minor "${CMAKE_MATCH_2}")
  if(dfu_version_major GREATER 65535 OR dfu_version_minor GREATER 65535)
    message(FATAL_ERROR "Dongle firmware major and minor versions must fit in 16 bits")
  endif()

  target_sources(app PRIVATE src/nordic_dfu_trigger.c)
  target_compile_definitions(app PRIVATE
    NORDIC_DFU_VERSION="${dfu_version}"
    NORDIC_DFU_VERSION_MAJOR=${dfu_version_major}
    NORDIC_DFU_VERSION_MINOR=${dfu_version_minor}
  )
endif()
