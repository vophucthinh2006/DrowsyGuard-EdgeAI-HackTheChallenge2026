# dms-sim-mcxn947 — hardware CAN test node (FRDM-MCXN947, DMS-AP stand-in).
#
# Same out-of-tree pattern as ../vcs-mcxn947/CMakeLists.txt: source lives in
# this repo, the MCUXpresso SDK checkout it builds against lives outside it
# (NPX_Workspace, see build.sh). Sibling project, same board, own
# board_port/ (trimmed to console+CAN+LED only, see board_port/pin_mux.c).
cmake_minimum_required(VERSION 3.22.0)

include(${SdkRootDirPath}/cmake/extension/mcux.cmake)

file(RELATIVE_PATH BOARD_PORT_REL_PATH
     ${SdkRootDirPath} ${CMAKE_CURRENT_SOURCE_DIR}/board_port)

project(dms_sim_mcxn947 LANGUAGES C CXX ASM
        PROJECT_BOARD_PORT_PATH ${BOARD_PORT_REL_PATH})

include(${SdkRootDirPath}/CMakeLists.txt)

mcux_add_source(
    BASE_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    SOURCES src/main.c

            src/icd/icd.h
            src/icd/icd.c
            src/icd/crc8.h
            src/icd/crc8.c

            src/can_test/can_test_link.h
            src/can_test/can_test_link.c
)

mcux_add_include(
    INCLUDES src
             src/icd
             src/can_test
)

# Produce a raw .bin next to the .elf so pyOCD can flash it directly.
mcux_convert_binary(BINARY ${APPLICATION_BINARY_DIR}/${MCUX_SDK_PROJECT_NAME}.bin)
