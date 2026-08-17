/*
 * Board-port macros for vcs-mcxn947 (FRDM-MCXN947 / VCS node).
 *
 * Every macro here names a peripheral/pin pair configured in
 * ../pin_mux.c — see that file for the UM12018 cross-reference each
 * assignment is based on, and ../../README.md for the full pin table.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _APP_H_
#define _APP_H_

#include "fsl_gpio.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
/*${macro:start}*/

/* ---- CAN0 (FLEXCAN0) — specs/04-interface-control-document.md ----------- */
#define CAN_LINK_BASEADDR CAN0
#define CAN_LINK_CLK_FREQ CLOCK_GetFlexcanClkFreq(0U)

/* ---- Motor drive (BTS7960 x2, PWM1 SM0/SM1/SM3) — specs/05 §2.2/VEH-001/VEH-001a --
 * Each BTS7960 module needs 2 PWM inputs (RPWM/LPWM) instead of 1 PWM + 2 direction
 * GPIOs. Left channel uses SM0's two channels (A+B); right channel's RPWM is SM1
 * channel A, but SM1's channel B (PWM1_B1) is not header-accessible on this board
 * (routed to the on-board FLEXSPI0 flash), so LPWM_R borrows SM3 channel A instead
 * — see board_port/pin_mux.c for the pin-signal cross-reference. */
#define MOTION_PWM_BASEADDR    PWM1
#define MOTION_PWM_SRC_CLK_FREQ CLOCK_GetFreq(kCLOCK_BusClk)

#define MOTION_EN_GPIO GPIO0
#define MOTION_EN_PIN  28U /* Arduino J2-D8, active-high, shared R_EN+L_EN on both modules */

/* ---- Alerts — specs/05 §5 -- simplified 2026-08-15 to buzzer+RGB LED only,
 * matching the system block diagram's "Local Warning System" box (no
 * vibration motor / fan relay / hazard lamps in scope) ---------------------- */
#define BUZZER_PWM_BASEADDR     PWM1 /* submodule 2, PORT2_2 / PWM1_A2, J3-7 */
#define BUZZER_PWM_SRC_CLK_FREQ CLOCK_GetFreq(kCLOCK_BusClk)

#define LED_RED_GPIO   GPIO0
#define LED_RED_PIN    10U
#define LED_GREEN_GPIO GPIO0
#define LED_GREEN_PIN  27U
#define LED_BLUE_GPIO  GPIO1
#define LED_BLUE_PIN   2U

/* ---- Gas/brake pedal simulation (SW2/SW3), active-low, onboard buttons -
 * Reuses the board's own onboard push buttons (UM12018 Table 4) instead of
 * external wiring -- both are documented as usable as plain GPIO inputs:
 *   SW2 = "Wakeup button", P0_23/WAKEUP_B -- also shared with Arduino
 *         header J4 pin 12 (ARD_A5) via SJ9, but SJ9's default position
 *         doesn't block reading P0_23 as GPIO, both paths see the same net.
 *   SW3 = "ISP mode switch", P0_6/ISPMODE_N-DEBUG -- UM12018 explicitly
 *         says it "can also act as a general-purpose input". Only holding
 *         it down DURING POWER-ON/RESET matters (the boot ROM samples it
 *         then to decide ISP vs. normal boot) -- reading it here, well
 *         after boot, in the running application, is exactly the
 *         documented use and has no such side effect. */
#define GAS_BUTTON_GPIO   GPIO0
#define GAS_BUTTON_PIN    23U /* SW2, onboard */
#define BRAKE_BUTTON_GPIO GPIO0
#define BRAKE_BUTTON_PIN  6U  /* SW3, onboard -- don't hold at power-on */

/* ---- Watchdog (WWDT0) — specs/05 VEH-052/053 ----------------------------- */
#define WWDT          WWDT0
#define IS_WWDT_RESET (0 != (CMC0->SRS & CMC_SRS_WWDT0_MASK))

/*${macro:end}*/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
/*${prototype:start}*/
void BOARD_InitHardware(void);
/*${prototype:end}*/

#endif /* _APP_H_ */
