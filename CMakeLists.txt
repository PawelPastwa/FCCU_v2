cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(Inzynierka)

# Pliki źródłowe (.c), które będą kompilowane do pamięci procesora
target_sources(app PRIVATE
        src/main.c
        src/ui/system_status.c
        src/can/can_interface.c
        src/can/can_definitions.c
)

# Foldery, w których kompilator ma szukać nagłówków (.h)
target_include_directories(app PRIVATE
        src
        src/ui
        src/can
)