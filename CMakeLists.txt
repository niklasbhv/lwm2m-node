#-------------------------------------------------------------------------------
# Zephyr Example Application
#
# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(openthread_coap_client_server)

FILE(GLOB app_sources src/*.c)

target_sources(app PRIVATE ${app_sources})

zephyr_linker_sources(DATA_SECTIONS src/sections-ram.ld)

target_include_directories(app PRIVATE ${ZEPHYR_BASE}/subsys/net/ip)
