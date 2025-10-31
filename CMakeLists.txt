cmake_minimum_required(VERSION 3.10)
project(MZPico-800-Manager C)

# Find z88dk compiler
find_program(ZCC_EXECUTABLE z88dk.zcc)
if(NOT ZCC_EXECUTABLE)
    message(FATAL_ERROR "z88dk.zcc not found. Make sure z88dk is installed and z88dk.zcc is in your PATH.")
endif()

# Optional test mode
option(ENABLE_TEST "Enable test mode" OFF)
if(ENABLE_TEST)
    set(TEST_DEFINE -DMZ800PICO_TEST)
endif()

# Common flags
set(COMMON_FLAGS
    +mz
    -lm
    -create-app
    -pragma-define:REGISTER_SP=0xd000
    --list
    -m
    ${TEST_DEFINE}
)

# Explorer target
set(EXPLORER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/mz-comm.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/console.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/manager.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/explorer.c"
)

set(EXPLORER_OUTPUT explorer)
set(EXPLORER_MZF "${CMAKE_CURRENT_BINARY_DIR}/${EXPLORER_OUTPUT}.mzf")

add_custom_command(
    OUTPUT ${EXPLORER_MZF}
    COMMAND ${ZCC_EXECUTABLE} ${COMMON_FLAGS} -o ${EXPLORER_OUTPUT} ${EXPLORER_SOURCES}
    DEPENDS ${EXPLORER_SOURCES}
    COMMAND ${CMAKE_COMMAND} -E rename ${EXPLORER_OUTPUT}.mzt ${EXPLORER_MZF}
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

add_custom_target(build_${EXPLORER_OUTPUT} ALL DEPENDS ${EXPLORER_MZF})

# Menu target
set(MENU_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/mz-comm.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/console.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/menu.c"
)

set(MENU_OUTPUT menu)
set(MENU_MZF "${CMAKE_CURRENT_BINARY_DIR}/${MENU_OUTPUT}.mzf")

add_custom_command(
    OUTPUT ${MENU_MZF}
    COMMAND ${ZCC_EXECUTABLE} ${COMMON_FLAGS} -o ${MENU_OUTPUT} ${MENU_SOURCES}
    DEPENDS ${MENU_SOURCES}
    COMMAND ${CMAKE_COMMAND} -E rename ${MENU_OUTPUT}.mzt ${MENU_MZF}
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

add_custom_target(build_${MENU_OUTPUT} ALL DEPENDS ${MENU_MZF})

set(MZF_FILES "${EXPLORER_MZF};${MENU_MZF}" PARENT_SCOPE)