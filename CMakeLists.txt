cmake_minimum_required(VERSION 3.10)

project(MZPico-800-Manager C)

# Set the path to z88dk's zcc compiler
find_program(ZCC_EXECUTABLE z88dk.zcc)

if(NOT ZCC_EXECUTABLE)
    message(FATAL_ERROR "z88dk.zcc not found. Make sure z88dk is installed and z88dk.zcc is in your PATH.")
endif()

# Enable optional define
option(ENABLE_TEST "Enable test mode" OFF)

set(SOURCE
    "${CMAKE_SOURCE_DIR}/mz-comm.c"
    "${CMAKE_SOURCE_DIR}/console.c"
    "${CMAKE_SOURCE_DIR}/explorer.c"
    "${CMAKE_SOURCE_DIR}/manager.c"
    "${CMAKE_SOURCE_DIR}/menu.c"
    "${CMAKE_SOURCE_DIR}/setup.c"
)

# Output binary name
set(OUTPUT mzpico800)

if(ENABLE_TEST)
    set(TEST_DEFINE -DMZ800PICO_TEST)
endif()

# Define the target (for example, targetting ZX Spectrum)
add_custom_command(
    OUTPUT ${OUTPUT}
    COMMAND ${ZCC_EXECUTABLE}
        +mz
        -lm
        -create-app
        -pragma-define:REGISTER_SP=0xd000
        --list
        -m
        ${TEST_DEFINE}
        -o ${OUTPUT}
        ${SOURCE}
    DEPENDS ${SOURCE}
    COMMAND ${CMAKE_COMMAND} -E rename ${OUTPUT}.mzt ${CMAKE_BINARY_DIR}/${OUTPUT}.mzf
    COMMENT "Building with z88dk..."
)

add_custom_target(build_${OUTPUT} ALL DEPENDS ${OUTPUT})

