# SPDX-License-Identifier: Apache-2.0

if(NOT "${OPENOCD}" MATCHES "^${ESPRESSIF_TOOLCHAIN_PATH}/.*")
    set(OPENOCD OPENOCD-NOTFOUND)
endif()
find_program(OPENOCD openocd PATHS
        ${ESPRESSIF_TOOLCHAIN_PATH}/openocd-esp32/bin
        ${ZEPHYR_SDK_INSTALL_DIR}/hosttools/openocd/bin
        NO_DEFAULT_PATH)

board_runner_args(openocd --config "interface/esp_usb_jtag.cfg")
board_runner_args(openocd --config "target/esp32s3.cfg")
board_runner_args(openocd --cmd-pre-init "init")

include(${ZEPHYR_BASE}/boards/common/esp32.board.cmake)
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)