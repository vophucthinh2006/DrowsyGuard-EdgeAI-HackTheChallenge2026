/*
 * Board-port pin muxing for dms-sim-mcxn947 (FRDM-MCXN947 / hardware CAN
 * test node).
 *
 * Deliberately trimmed to only what this test tool needs -- debug console
 * UART, FLEXCAN0, and the RGB status LED for a link-fresh indicator. No
 * motor/alert/button pins: this board plays DMS-AP, not VCS, and has
 * nothing wired to those headers. Pin choices and electrical config are
 * copied from ../../vcs-mcxn947/board_port/pin_mux.c (identical physical
 * board, identical on-board CAN transceiver/header) -- see that file and
 * ../../vcs-mcxn947/README.md for the UM12018 cross-references.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "pin_mux.h"

#include "fsl_common.h"
#include "fsl_gpio.h"
#include "fsl_port.h"

void BOARD_InitBootPins(void) { BOARD_InitPins(); }

static const port_pin_config_t kGpioOutConfig = {
    kPORT_PullDisable, kPORT_LowPullResistor, kPORT_FastSlewRate,
    kPORT_PassiveFilterDisable, kPORT_OpenDrainDisable, kPORT_LowDriveStrength,
    kPORT_MuxAlt0, kPORT_InputBufferEnable, kPORT_InputNormal, kPORT_UnlockRegister};

static void InitGpioOutput(PORT_Type *port, GPIO_Type *gpio, uint32_t pin,
                            uint32_t initLogic) {
  PORT_SetPinConfig(port, pin, &kGpioOutConfig);
  gpio_pin_config_t cfg = {.pinDirection = kGPIO_DigitalOutput, .outputLogic = initLogic};
  GPIO_PinInit(gpio, pin, &cfg);
}

void BOARD_InitPins(void) {
  CLOCK_EnableClock(kCLOCK_Port0);
  CLOCK_EnableClock(kCLOCK_Port1);
  CLOCK_EnableClock(kCLOCK_Gpio0);
  CLOCK_EnableClock(kCLOCK_Gpio1);

  /* ---- Debug console (LPUART4 / FLEXCOMM4), MCU-Link virtual COM port ---
   * Same pin pair and same "PRINTF silently disappears without this" trap
   * as vcs-mcxn947 -- see that project's pin_mux.c for the story. */
  const port_pin_config_t debugUartPinConfig = {
      kPORT_PullDisable, kPORT_LowPullResistor, kPORT_FastSlewRate,
      kPORT_PassiveFilterDisable, kPORT_OpenDrainDisable, kPORT_LowDriveStrength,
      kPORT_MuxAlt2, kPORT_InputBufferEnable, kPORT_InputNormal, kPORT_UnlockRegister};
  PORT_SetPinConfig(PORT1, 8U, &debugUartPinConfig); /* FC4_P0 / LPUART4 TXD */
  PORT_SetPinConfig(PORT1, 9U, &debugUartPinConfig); /* FC4_P1 / LPUART4 RXD */

  /* ---- CAN0 (FLEXCAN0), on-board transceiver, header J10 ---------------- */
  const port_pin_config_t canPinConfig = {
      kPORT_PullDisable, kPORT_LowPullResistor, kPORT_FastSlewRate,
      kPORT_PassiveFilterDisable, kPORT_OpenDrainDisable, kPORT_LowDriveStrength,
      kPORT_MuxAlt11, kPORT_InputBufferEnable, kPORT_InputNormal, kPORT_UnlockRegister};
  PORT_SetPinConfig(PORT1, 10U, &canPinConfig); /* CAN0_TXD */
  PORT_SetPinConfig(PORT1, 11U, &canPinConfig); /* CAN0_RXD */

  /* ---- Status LED (active-LOW): green = fresh VCS_STATUS RX, red = stale/none. */
  InitGpioOutput(PORT0, GPIO0, 10U, 1U); /* red,   off */
  InitGpioOutput(PORT0, GPIO0, 27U, 1U); /* green, off */
  InitGpioOutput(PORT1, GPIO1, 2U, 1U);  /* blue,  off (unused, reserved) */
}
