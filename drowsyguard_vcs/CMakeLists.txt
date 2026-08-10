# drowsyguard_vcs — Vehicle Control Simulator node (FRDM-MCXN947).
#
# Out-of-tree ("freestanding") MCUXpresso SDK application: the source lives
# in this git repo, outside the SDK checkout, and only refers back into the
# SDK through ${SdkRootDirPath} (see build.sh for how that's located --
# NPX_Workspace, not this repo). Follows the same pattern as the touch_rgb
# and wifi_sensing_npu projects in that workspace.
#
# PROJECT_BOARD_PORT_PATH points at our own board_port/, which holds the pin
# muxing, clock setup and app.h that would normally live under the SDK's
# examples/_boards/<board>/ tree.
cmake_minimum_required(VERSION 3.22.0)

include(${SdkRootDirPath}/cmake/extension/mcux.cmake)

# The SDK resolves PROJECT_BOARD_PORT_PATH against ${SdkRootDirPath}, so it
# has to be a RELATIVE path — an absolute one silently resolves wrong and
# pin_mux.c is not found (specs/02-development-standards.md DEV-034).
file(RELATIVE_PATH BOARD_PORT_REL_PATH
     ${SdkRootDirPath} ${CMAKE_CURRENT_SOURCE_DIR}/board_port)

project(drowsyguard_vcs LANGUAGES C CXX ASM
        PROJECT_BOARD_PORT_PATH ${BOARD_PORT_REL_PATH})

include(${SdkRootDirPath}/CMakeLists.txt)

mcux_add_source(
    BASE_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    SOURCES src/main.c

            src/icd/icd.h
            src/icd/icd.c
            src/icd/crc8.h
            src/icd/crc8.c

            src/can_link/can_link.h
            src/can_link/can_link.c

            src/safety/safety.h
            src/safety/safety.c

            src/motion/motion.h
            src/motion/motion.c

            src/alerts/alerts.h
            src/alerts/alerts.c
)

# So src/main.c and friends can #include "icd/icd.h", "can_link/can_link.h"
# etc. by their path relative to src/. mcux_add_include() with no BASE_PATH
# resolves each entry against ${CMAKE_CURRENT_LIST_DIR} itself, so these are
# given relative -- passing an already-absolute path here gets the project
# directory prepended twice (caught by an actual build failure, not by
# inspection: "fatal error: icd.h: No such file or directory" with a visibly
# doubled -I path).
mcux_add_include(
    INCLUDES src
             src/icd
             src/can_link
             src/safety
             src/motion
             src/alerts
)

# Produce a raw .bin next to the .elf so pyOCD can flash it directly.
mcux_convert_binary(BINARY ${APPLICATION_BINARY_DIR}/${MCUX_SDK_PROJECT_NAME}.bin)
