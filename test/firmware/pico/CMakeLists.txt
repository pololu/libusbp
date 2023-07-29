cmake_minimum_required(VERSION 3.13)
if (DEFINED ENV{PICO_SDK_PATH})
  include("$ENV{PICO_SDK_PATH}/pico_sdk_init.cmake")
else ()
  include("../pico-sdk/pico_sdk_init.cmake")
endif()
project(project C CXX ASM)
add_compile_options(-Wall -Wextra)
pico_sdk_init()

add_executable(usb_test_device_a main.c stdio_uart_buf.c)

target_compile_definitions(usb_test_device_a PRIVATE STDIO_UART_BUF_SIZE_TX=65536)

# Make sure TinyUSB can find tusb_config.h
target_include_directories(usb_test_device_a PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(usb_test_device_a pico_stdlib pico_unique_id tinyusb_device)

pico_add_extra_outputs(usb_test_device_a)
