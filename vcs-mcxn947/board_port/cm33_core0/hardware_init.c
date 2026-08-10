/*
 * Copyright 2022 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*${header:start}*/
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_clock.h"
/*${header:end}*/

/*${function:start}*/
void BOARD_InitHardware(void) {
  /* attach FRO 12M to FLEXCOMM4 (debug console) */
  CLOCK_SetClkDiv(kCLOCK_DivFlexcom4Clk, 1u);
  CLOCK_AttachClk(BOARD_DEBUG_UART_CLK_ATTACH);

  /* attach PLL1Clk0 to FLEXCAN0 -- identical to the SDK's own
   * examples/_boards/frdmmcxn947/driver_examples/flexcan/interrupt_transfer
   * clock setup, which is why BOARD_BootClockPLL100M() (not the
   * BOARD_InitBootClocks()/PLL150M variant used elsewhere in this
   * workspace) is used below: it's the PLL configuration that reference
   * example's FLEXCAN0 divider math was verified against. */
  CLOCK_SetClkDiv(kCLOCK_DivPLL1Clk0, 2U);
  CLOCK_SetClkDiv(kCLOCK_DivFlexcan0Clk, 1U);
  CLOCK_AttachClk(kPLL1_CLK0_to_FLEXCAN0);

  BOARD_InitPins();
  BOARD_BootClockPLL100M();
  BOARD_InitDebugConsole();

  /* PWM1 sub-clocks (all 3 submodules used: motor L/R + buzzer) — required
   * or the FlexPWM counters never run, per
   * examples/_boards/frdmmcxn947/driver_examples/pwm/cm33_core0/hardware_init.c */
  SYSCON->PWM1SUBCTL |=
      (SYSCON_PWM1SUBCTL_CLK0_EN_MASK | SYSCON_PWM1SUBCTL_CLK1_EN_MASK |
       SYSCON_PWM1SUBCTL_CLK2_EN_MASK);
}
/*${function:end}*/
