cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(Lora_Transmit)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE lib/llcc68_driver.c)

target_include_directories(app PRIVATE lib)
