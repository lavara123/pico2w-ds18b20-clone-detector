# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "2.1.0")
    message(FATAL_ERROR "Pico SDK 2.1.0 or newer is required (tested with 2.3.1). Found ${PICO_SDK_VERSION_STRING}")
endif()

project(ds18b20_clone_detector C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

set(DSCD_VERSION "1.0.0")

add_executable(ds18b20_clone_detector
    src/main.c
    src/onewire.c
    src/ds18b20_tests.c
    src/status_led.c
)

pico_set_program_name(ds18b20_clone_detector "ds18b20_clone_detector")
pico_set_program_version(ds18b20_clone_detector "${DSCD_VERSION}")
pico_set_program_description(ds18b20_clone_detector
    "Detects counterfeit/clone DS18B20 sensors (port of cpetrich/counterfeit_DS18B20)")
pico_set_program_url(ds18b20_clone_detector
    "https://github.com/cpetrich/counterfeit_DS18B20")

target_compile_definitions(ds18b20_clone_detector PRIVATE
    DSCD_VERSION="${DSCD_VERSION}"
)

target_compile_options(ds18b20_clone_detector PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Serial console over USB (default) and UART0 (GP0/GP1)
pico_enable_stdio_usb(ds18b20_clone_detector 1)
pico_enable_stdio_uart(ds18b20_clone_detector 1)

target_include_directories(ds18b20_clone_detector PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(ds18b20_clone_detector
    pico_stdlib
    pico_multicore
    hardware_gpio
    hardware_sync
)

# The on-board LED of the Pico W / Pico 2 W is wired to the CYW43 radio chip.
if (PICO_CYW43_SUPPORTED)
    target_link_libraries(ds18b20_clone_detector pico_cyw43_arch_none)
else()
    message(WARNING "Board '${PICO_BOARD}' has no CYW43 - falling back to PICO_DEFAULT_LED_PIN")
endif()

pico_add_extra_outputs(ds18b20_clone_detector)
