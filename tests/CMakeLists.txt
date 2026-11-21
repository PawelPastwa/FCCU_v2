cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fccu_unit_tests)

#${CMAKE_CURRENT_SOURCE_DIR}/../src zamiast ../src
#To dobra praktyka w CMake i Zephyrze. Zwykłe ../src zadziała przy prostym west build, ale potrafi się wywalić przy uruchamianiu testów przez west twister albo przy budowaniu z innego katalogu roboczego. ${CMAKE_CURRENT_SOURCE_DIR} wymusza zawsze pełną, jednoznaczną ścieżkę względem położenia pliku CMakeLists.txt.

target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/core
)

target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/fccu_state.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/fccu_valves.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/fccu_fan.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/fccu_flow.c
        src/test_fccu_state.c
        src/test_fccu_valves.c
        src/test_fccu_fan.c
        src/test_fccu_flow.c
        src/test_fccu_scaling.c
)