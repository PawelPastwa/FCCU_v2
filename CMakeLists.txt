cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(Inzynierka)

# Pliki źródłowe (.c), które będą kompilowane do pamięci procesora
target_include_directories(app PRIVATE
        src
        src/core
        src/bsp
        src/comm
)

target_sources(app PRIVATE
        src/main.c
        src/core/fccu_state.c
        src/core/fccu_valves.c
        src/core/fccu_fan.c
        src/core/fccu_flow.c
        src/bsp/fccu_actuators.c
        src/bsp/fccu_button.c
        src/bsp/fccu_sensors.c
        src/bsp/fccu_storage.c
        src/bsp/system_status.c
        src/bsp/fccu_watchdog.c
        src/comm/fccu_can.c
        src/comm/dbc/hydrogreen_can_definitions.c

)

# Rygorystyczna kontrola jakości kodu aplikacji
target_compile_options(app PRIVATE -Wall -Wextra -Werror)

# Wyłączenie -Werror dla kodu generowanego automatycznie z DBC
set_source_files_properties(
        src/comm/dbc/hydrogreen_can_definitions.c
        PROPERTIES COMPILE_OPTIONS "-Wno-error"
)