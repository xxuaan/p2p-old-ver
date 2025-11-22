cmake_minimum_required(VERSION 3.13)

# This line MUST come before the SDK import
set(PICO_BOARD pico_w)

# Now import the SDK
include(pico_sdk_import.cmake)

project(picow_network C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Optional: detect Paho MQTT-SN library in repository (keep its sources untouched)
set(PAHO_DIR_ROOT "${CMAKE_CURRENT_LIST_DIR}/lib/paho.mqtt-sn.embedded-c")
set(PAHO_DIR_BUILD "${CMAKE_CURRENT_LIST_DIR}/build/lib/paho.mqtt-sn.embedded-c")

if (EXISTS "${PAHO_DIR_ROOT}")
  set(PAHO_DIR "${PAHO_DIR_ROOT}")
elseif (EXISTS "${PAHO_DIR_BUILD}")
  set(PAHO_DIR "${PAHO_DIR_BUILD}")
else()
  set(PAHO_DIR "")
endif()

if (PAHO_DIR)
  message(STATUS "Paho MQTT-SN detected at ${PAHO_DIR}")
  file(GLOB PAHO_MQTTSN_PACKET_SRCS "${PAHO_DIR}/MQTTSNPacket/src/*.c")
  file(GLOB PAHO_MQTTSN_CLIENT_SRCS "${PAHO_DIR}/MQTTSNClient/src/*.c")

  add_library(mqttsn_paho STATIC ${PAHO_MQTTSN_PACKET_SRCS} ${PAHO_MQTTSN_CLIENT_SRCS})
  target_include_directories(mqttsn_paho PUBLIC ${PAHO_DIR}/MQTTSNPacket/src ${PAHO_DIR}/MQTTSNClient/src ${PAHO_DIR})
else()
  message(STATUS "Paho MQTT-SN not found; build will proceed without MQTT-SN support")
endif()


# Add FatFs library
add_library(fatfs STATIC
    ${CMAKE_CURRENT_LIST_DIR}/lib/fatfs/source/ff.c
    ${CMAKE_CURRENT_LIST_DIR}/lib/fatfs/source/diskio_sdcard.c
    ${CMAKE_CURRENT_LIST_DIR}/lib/fatfs/source/ffsystem.c
    ${CMAKE_CURRENT_LIST_DIR}/lib/fatfs/source/ffunicode.c
)
target_include_directories(fatfs PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/lib/fatfs/source
)


add_executable(picow_network
  main.c
  wifi_driver.c
  udp_driver.c
  mqttsn_adapter.c
  mqttsn_client.c
  block_transfer.c
  sd_card.c
)

pico_enable_stdio_usb(picow_network 1)
pico_enable_stdio_uart(picow_network 0)

pico_add_extra_outputs(picow_network)

target_include_directories(picow_network PRIVATE ${CMAKE_CURRENT_LIST_DIR} )

target_link_libraries(picow_network PRIVATE
    pico_cyw43_arch_lwip_threadsafe_background 
    pico_stdlib
    hardware_adc
    hardware_spi
    hardware_gpio
    fatfs
)

# If Paho was detected, link the library and provide a compile macro so
# example code can conditionally include Paho headers.
if (EXISTS "${PAHO_DIR}")
  target_link_libraries(picow_network PRIVATE mqttsn_paho)
  target_compile_definitions(picow_network PRIVATE HAVE_PAHO=1)
endif()

# Subscriber executable
add_executable(picow_subscriber
  subscriber_main.c
  wifi_driver.c
  udp_driver.c
  mqttsn_adapter.c
  mqttsn_client.c
  block_transfer.c
  sd_card.c
)

pico_enable_stdio_usb(picow_subscriber 1)
pico_enable_stdio_uart(picow_subscriber 0)

pico_add_extra_outputs(picow_subscriber)

target_include_directories(picow_subscriber PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(picow_subscriber PRIVATE
    pico_cyw43_arch_lwip_threadsafe_background 
    pico_stdlib
    hardware_gpio
    hardware_spi
    fatfs
)

if (EXISTS "${PAHO_DIR}")
  target_link_libraries(picow_subscriber PRIVATE mqttsn_paho)
  target_compile_definitions(picow_subscriber PRIVATE HAVE_PAHO=1)
endif()