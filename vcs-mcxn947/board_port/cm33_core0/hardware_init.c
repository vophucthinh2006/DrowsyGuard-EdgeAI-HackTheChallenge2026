/*
 * Copyright 2022 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*${header:start}*/
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_clock.h"
#include "fsl_reset.h"
/*${header:end}*/

/*${function:start}*/
void BOARD_InitHardware(void) {
  /* attach FRO 12M to FLEXCOMM4 (debug console) */
  CLOCK_SetClkDiv(kCLOCK_DivFlexcom4Clk, 1u);
  CLOCK_AttachClk(BOARD_DEBUG_UART_CLK_ATTACH);

  /* WWDT0 functional clock (specs/05 VEH-052) -- found missing only by
   * actually flashing to hardware: without this, WWDT_Init()'s internal
   * `while (base->TV == 0xFFUL) {}` spin (fsl_wwdt.c) waits forever for a
   * clock that was never running, hanging boot silently between the
   * "[can] FlexCAN0 up" and "[safety] WWDT armed" PRINTF lines -- no error,
   * no crash, just silence (the exact VEH-003 failure mode: "a peripheral
   * with no clock reads back zeros and produces no error"). Matches the
   * SDK's own driver_examples/wwdt/cm33_core0/hardware_init.c for this
   * board. */
  CLOCK_SetClkDiv(kCLOCK_DivWdt0Clk, 1U);
  SYSCON->CLOCK_CTRL |= SYSCON_CLOCK_CTRL_FRO1MHZ_CLK_ENA_MASK;

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

  /* PWM1 top-level AHB clock gate -- separate from, and a PREREQUISITE for,
   * the per-submodule SYSCON_PWM1SUBCTL gates right below. Found 2026-08-15
   * chasing "not one single PWM1 channel produces any output at all"
   * (buzzer AND all 4 motor RPWM/LPWM channels silent) after every other
   * candidate (pin mux -- matches the SDK's own proven pwm example
   * line-for-line --, OUTEN, LDOK bitmask, per-submodule sub-clocks, and
   * the 100 MHz source clock fed to PWM_SetupPwm()) checked out correct.
   * fsl_clock.h defines `kCLOCK_Pwm1 = CLK_GATE_DEFINE(AHB_CLK_CTRL3, 7)`
   * as a DIFFERENT gate from `kCLOCK_Pwm1_Sm0..3` (which map onto the same
   * SYSCON_PWM1SUBCTL bits already being set) -- without this, register
   * writes to PWM1 can appear to "succeed" (PWM_Init() never returns
   * kStatus_Fail) while the peripheral's actual counter/output logic never
   * runs, on every submodule at once. Same failure class as the WWDT0 and
   * PWM1 SM3 clock bugs above: a peripheral with no functional clock reads
   * back zeros and produces no error. */
  CLOCK_EnableClock(kCLOCK_Pwm1);

  /* PWM1 reset deassert -- SEPARATE mechanism from clock gating entirely.
   * Added 2026-08-15 right after CLOCK_EnableClock(kCLOCK_Pwm1) alone
   * didn't fix "not one PWM1 channel produces any output" either. Many Arm
   * SoCs hold most peripherals in reset at power-on and need BOTH an
   * explicit clock-gate-enable AND an explicit reset-deassert before the
   * block's internal logic runs -- having only one of the two can still
   * leave register writes silently inert. fsl_reset.h defines
   * `kPWM1_RST_SHIFT_RSTn` as PWM1's own reset control bit, never called
   * anywhere in this project before now. */
  RESET_ClearPeripheralReset(kPWM1_RST_SHIFT_RSTn);

  /* PWM1 sub-clocks -- required or the FlexPWM counters never run, per
   * examples/_boards/frdmmcxn947/driver_examples/pwm/cm33_core0/hardware_init.c.
   * BUG FOUND 2026-08-15 on real hardware ("PWM khong ra xung" on the right
   * channel's LPWM only): this project actually uses FOUR submodules, not
   * three -- SM0 (left RPWM+LPWM), SM1 (right RPWM), SM2 (buzzer), AND SM3
   * (right LPWM, borrowed because SM1's own B channel isn't header-
   * accessible, see app.h/motion.c). The old comment said "3 submodules"
   * and CLK3_EN was never OR'd in, so SM3's counter never ran -- right LPWM
   * silently produced 0% duty forever regardless of what motion.c wrote to
   * it (right RPWM/left channel/buzzer all worked, since SM0/1/2 did have
   * their clocks). Exact same failure class as the WWDT0 clock bug above:
   * a peripheral with no functional clock reads back zeros, no error. */
  SYSCON->PWM1SUBCTL |=
      (SYSCON_PWM1SUBCTL_CLK0_EN_MASK | SYSCON_PWM1SUBCTL_CLK1_EN_MASK |
       SYSCON_PWM1SUBCTL_CLK2_EN_MASK | SYSCON_PWM1SUBCTL_CLK3_EN_MASK);

  /* ROOT CAUSE, found 2026-08-15 via a live register dump (not one PWM1
   * channel -- buzzer or motors -- ever produced output despite OUTEN/MCTRL/
   * VAL* all reading back correct): PWM1->SM[n].DISMAP[0] resets to 0xFFFF
   * on this silicon -- EVERY one of the 4 fault inputs is mapped to disable
   * EVERY channel (A/B/X) on every submodule, and FCTRL2's NOCOMB bits
   * default to 0 (combinational path enabled), so the *live, unlatched*
   * state of the fault pins gates the output in real time. FSTS read back
   * 0xF00 (FFPIN, the raw pin bits) with FFLAG (the latched flag, bits 0-3)
   * at 0 -- i.e. no fault was ever latched, the 4 fault inputs are simply
   * floating/unconnected and read as "fault asserted" by default polarity.
   * This project has no hardware wired to PWM1's fault inputs at all, so
   * the correct fix is to clear the disable mapping on every submodule we
   * use, not to replicate the SDK's own pwm_3ph example (which happens to
   * dodge this by explicitly setting DEMO_PWM_FAULT_LEVEL=true, flipping
   * the polarity so the same floating-high read is interpreted as "no
   * fault" -- fragile, and unnecessary when no fault sensor exists). */
  PWM1->SM[0].DISMAP[0] = 0U;
  PWM1->SM[1].DISMAP[0] = 0U;
  PWM1->SM[2].DISMAP[0] = 0U;
  PWM1->SM[3].DISMAP[0] = 0U;
}
/*${function:end}*/
