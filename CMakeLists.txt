cmake_minimum_required(VERSION 3.6)

## Version Configuration.
## Define e.g. 0.72 as major 0 and minor 72
set(VERSION_MAJOR 0)
set(VERSION_MINOR 77)

## Release Type Selection
option(DH_DEBUG "Build a debug version" OFF)
option(DH_DEBUG_CDC_FLASH "Enable CDC command to trigger bootloader mode" OFF)
option(DH_DEBUG_TRACKPAD "Log HID descriptors and Apple HID reports for trackpad work (requires DH_DEBUG)" OFF)
option(DH_TRACKPAD_PHASE1 "Apple Magic Trackpad gesture translation: multi-touch decode, two-finger scroll, three-finger workspace swipe. Default ON. Disable with -DDH_TRACKPAD_PHASE1=OFF to fall back to plain-mouse behavior for the trackpad." ON)
option(DH_TRACKPAD_GESTURES "Path G: faithful port of libinput's gesture state machine, scoped to 2F scroll + 3F/4F swipe (pinch / hold / 3FG-drag intentionally deferred). Replaces Phase 1's hand-rolled scroll/swipe paths when on. OFF by default." OFF)
option(DH_TRACKPAD_TAP_TO_CLICK "Path A: faithful port of libinput's tap state machine. Adds tap-to-click, tap-and-drag, drag-lock, etc. once translation is complete. Compiles in src/magic_trackpad_tap.c. OFF by default; default builds keep flash budget unchanged." OFF)

## Hardware Configuration
set(DP_PIN_DEFAULT 14 CACHE STRING "Default USB D+ Pin Number")
set(PIO_USE_TINYUSB 1 CACHE STRING "Make TinyUSB Manage the PIO USB Port")
set(PICO_BOARD "pico")

## Pico SDK Configuration
set(PICO_SDK_FETCH_FROM_GIT off)
set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/pico-sdk)
set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/src)
include(${PICO_SDK_PATH}/pico_sdk_import.cmake)

## Project Setup
project(deskhop_project C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(PICO_COPY_TO_RAM 1)

## Compiler Flags
set(CMAKE_C_FLAGS "-Ofast -Wall -mcpu=cortex-m0plus -mtune=cortex-m0plus -fstack-usage")

## Initialize Pico SDK
pico_sdk_init()

## PIO USB Library Setup
set(PICO_PIO_USB_DIR ${CMAKE_CURRENT_LIST_DIR}/Pico-PIO-USB)

add_library(Pico-PIO-USB STATIC
  ${PICO_PIO_USB_DIR}/src/pio_usb.c
  ${PICO_PIO_USB_DIR}/src/pio_usb_host.c
  ${PICO_PIO_USB_DIR}/src/usb_crc.c
)
pico_generate_pio_header(Pico-PIO-USB ${PICO_PIO_USB_DIR}/src/usb_tx.pio)
pico_generate_pio_header(Pico-PIO-USB ${PICO_PIO_USB_DIR}/src/usb_rx.pio)

target_link_libraries(Pico-PIO-USB PRIVATE
  pico_stdlib
  pico_multicore
  hardware_pio
  hardware_dma
)
target_include_directories(Pico-PIO-USB PRIVATE ${PICO_PIO_USB_DIR})

## Source Files
set(COMMON_SOURCES
  ${SRC_DIR}/usb_descriptors.c
  ${SRC_DIR}/defaults.c
  ${SRC_DIR}/constants.c
  ${SRC_DIR}/protocol.c
  ${SRC_DIR}/hid_parser.c
  ${SRC_DIR}/hid_report.c
  ${SRC_DIR}/utils.c
  ${SRC_DIR}/handlers.c
  ${SRC_DIR}/setup.c
  ${SRC_DIR}/keyboard.c
  ${SRC_DIR}/mouse.c
  ${SRC_DIR}/tasks.c
  ${SRC_DIR}/led.c
  ${SRC_DIR}/uart.c
  ${SRC_DIR}/usb.c
  ${SRC_DIR}/main.c
  ${SRC_DIR}/ramdisk.c
  ${SRC_DIR}/magic_trackpad.c
  ${SRC_DIR}/magic_trackpad_tap.c
  ${SRC_DIR}/magic_trackpad_gestures.c
  ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/dcd_pio_usb.c
  ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c
)

## Include Directories
set(COMMON_INCLUDES
  ${SRC_DIR}/include
  ${PICO_PIO_USB_DIR}/src
)

## Library Dependencies
set(COMMON_LINK_LIBRARIES
  pico_stdlib
  hardware_flash
  hardware_uart
  hardware_gpio
  hardware_pio
  hardware_dma

  tinyusb_device 
  tinyusb_host
  pico_multicore
  pico_unique_id
  Pico-PIO-USB
)
set(binary deskhop)

## Disk Image Configuration
# This assembles disk.S, then updates the elf section in post-build
# With the disk FAT image binary in /disk/disk.img 

set(DISK_ASM "${CMAKE_CURRENT_LIST_DIR}/disk/disk.S")
set(DISK_BIN "${CMAKE_CURRENT_LIST_DIR}/disk/disk.img")
set_property(SOURCE ${DISK_ASM} APPEND PROPERTY COMPILE_OPTIONS "-x" "assembler-with-cpp")

add_executable(${binary} ${DISK_ASM})

target_sources(${binary} PUBLIC ${COMMON_SOURCES})
target_compile_definitions(${binary} 
    PRIVATE
    VERSION_MAJOR=${VERSION_MAJOR}
    VERSION_MINOR=${VERSION_MINOR}
    PIO_USB_USE_TINYUSB=${PIO_USE_TINYUSB}
    PIO_USB_DP_PIN_DEFAULT=${DP_PIN_DEFAULT}
    __disk_file_path__="${DISK_BIN}"
)

## Support building a debug version
if (DH_DEBUG)
  add_definitions(-DDH_DEBUG)  
endif()

if (DH_DEBUG_CDC_FLASH)
  add_definitions(-DDH_DEBUG_CDC_FLASH)
endif()

if (DH_DEBUG_TRACKPAD)
  if (NOT DH_DEBUG)
    message(FATAL_ERROR "DH_DEBUG_TRACKPAD requires DH_DEBUG (CDC log sink). Re-run cmake with -DDH_DEBUG=ON.")
  endif()
  add_definitions(-DDH_DEBUG_TRACKPAD)
endif()

if (DH_TRACKPAD_PHASE1)
  add_definitions(-DDH_TRACKPAD_PHASE1)
endif()

if (DH_TRACKPAD_TAP_TO_CLICK)
  add_definitions(-DDH_TRACKPAD_TAP_TO_CLICK)
endif()

if (DH_TRACKPAD_GESTURES)
  if (NOT DH_TRACKPAD_PHASE1)
    message(FATAL_ERROR "DH_TRACKPAD_GESTURES requires DH_TRACKPAD_PHASE1=ON (it replaces Phase 1's scroll/swipe paths in-place).")
  endif()
  add_definitions(-DDH_TRACKPAD_GESTURES)
endif()
  
target_include_directories(${binary} PUBLIC ${COMMON_INCLUDES})
target_link_libraries(${binary} PUBLIC ${COMMON_LINK_LIBRARIES})

## Configure Pico Library
pico_enable_stdio_usb(${binary} 0)
pico_enable_stdio_uart(${binary} 0)
pico_set_linker_script(${binary} ${CMAKE_SOURCE_DIR}/misc/memory_map.ld)

## Build other file formats as well
pico_add_extra_outputs(${binary})

# For internal firmware use, to simplify comparison: 0.72 -> 172
math(EXPR FIRMWARE_VERSION "${VERSION_MAJOR} * 1000 + ${VERSION_MINOR} + 100")

## Post-Build Commands
add_custom_command(
    TARGET ${binary} POST_BUILD
    COMMAND python3 ${CMAKE_SOURCE_DIR}/misc/crc32.py ${binary}.bin ${binary}.crc ${FIRMWARE_VERSION}
    COMMAND ${CMAKE_OBJCOPY} --update-section .section_metadata=${binary}.crc ${binary}.elf
    COMMAND ${CMAKE_OBJCOPY} -O binary ${binary}.elf ${binary}.bin
    COMMAND ${CMAKE_BINARY_DIR}/elf2uf2/elf2uf2 ${binary}.elf ${binary}.uf2
    COMMENT "Update CRC32 section to match the actual binary"  
)

## Linker Options
target_link_options(${binary} PRIVATE
  -Xlinker
  --print-memory-usage
)

