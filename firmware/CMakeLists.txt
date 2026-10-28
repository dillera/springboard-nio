cmake_minimum_required(VERSION 3.21)

# The pinned pico-sdk and apio come from mozzwald's bridge checkout.
set(SBNIO_DEPS "${CMAKE_CURRENT_LIST_DIR}/../repos/fujinet-nio/bridges/rp2350-springboard/.deps"
    CACHE PATH "Directory holding pico-sdk and apio")
if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH "${SBNIO_DEPS}/pico-sdk")
endif()
set(PICO_BOARD waveshare_core2350b)
set(PICO_PLATFORM rp2350-arm-s)
include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
project(sbnio C CXX ASM)
pico_sdk_init()

set(SBNIO_ROM "${CMAKE_CURRENT_LIST_DIR}/../palm/build/sbnio.rom" CACHE FILEPATH
    "Springboard ROM image served on CS0")
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rom_image.c"
    COMMAND python3 "${CMAKE_CURRENT_LIST_DIR}/tools/rom2c.py" "${SBNIO_ROM}"
            "${CMAKE_CURRENT_BINARY_DIR}/rom_image.c"
    DEPENDS "${SBNIO_ROM}" "${CMAKE_CURRENT_LIST_DIR}/tools/rom2c.py")

add_executable(sbnio
    src/main.c
    src/bus.c
    src/mailbox.c
    src/console.c
    src/usb_descriptors.c
    src/pio_rearm_program.c
    src/persistent_reply.S
    "${CMAKE_CURRENT_BINARY_DIR}/rom_image.c")
target_include_directories(sbnio PRIVATE src ../include "${SBNIO_DEPS}/apio/include")
target_compile_options(sbnio PRIVATE -Wall -Wextra)
# Same reply marker as the proven msc-003 build. Core1's stack shares
# SCRATCH_X with its reply loop, so it gets 2 KiB of the 4.
target_compile_definitions(sbnio PRIVATE
    SB_PIO_REPLY_MARKER=1
    PICO_STACK_SIZE=4096 PICO_CORE1_STACK_SIZE=2048
    PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=1
    PICO_USB_RESET_INCLUDE_DEFAULT_APP_DRIVER_CB=1
    PICO_USB_RESET_SUPPORT_MS_OS_20_DESCRIPTOR=0
    PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=0)
target_link_libraries(sbnio PRIVATE
    pico_stdlib pico_multicore pico_unique_id pico_usb_reset
    hardware_pio hardware_dma hardware_clocks tinyusb_device)
target_link_options(sbnio PRIVATE -Wl,-z,noexecstack)
# GP0 is the Springboard /IRQ: never let the SDK put a UART on it.
pico_enable_stdio_uart(sbnio 0)
pico_enable_stdio_usb(sbnio 0)
pico_set_binary_type(sbnio copy_to_ram)
pico_add_extra_outputs(sbnio)
