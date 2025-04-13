cmake_minimum_required(VERSION 3.13)

# initialize pico-sdk from submodule
include(pico-sdk/pico_sdk_init.cmake)

project(stimmgabeluhr)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# lib
add_subdirectory(lib/interfaces)
add_subdirectory(lib/mcp23017)


# rest of your project
file(GLOB stimmgabeluhr_LIB
        "lib/*.h"
        "lib/*.cpp"
)

file(GLOB stimmgabeluhr_SRC
        "src/*.h"
        "src/*.cpp"
)

file(GLOB stimmgabeluhr_PIOS
        "lib/*.pio"
#         "src/*.pio"
)

add_executable(stimmgabeluhr
        ${stimmgabeluhr_SRC}
        ${stimmgabeluhr_LIB}
)
target_include_directories(stimmgabeluhr
        PRIVATE "."

)


# enable USB serial output
pico_enable_stdio_usb(stimmgabeluhr 1)
pico_enable_stdio_uart(stimmgabeluhr 0)

# compile pio file
pico_generate_pio_header(stimmgabeluhr
        ${stimmgabeluhr_PIOS}
        OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/generated
)

target_link_libraries(stimmgabeluhr
        pico_stdlib
        pico_mcp23017
        hardware_pio
        hardware_dma
        hardware_irq
        hardware_rtc
        hardware_i2c
        pico_multicore
        )

pico_set_program_name(stimmgabeluhr "Stimmgabeluhr")
pico_set_program_version(stimmgabeluhr "0.2")

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(stimmgabeluhr)