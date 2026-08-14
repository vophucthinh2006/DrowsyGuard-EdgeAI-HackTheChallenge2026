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

/* ---- Alerts — specs/05 §5 ------------------------------------------------ */
#define BUZZER_PWM_BASEADDR     PWM1 /* submodule 2, PORT2_2 / PWM1_A2, J3-7 */
#define BUZZER_PWM_SRC_CLK_FREQ CLOCK_GetFreq(kCLOCK_BusClk)

#define LED_RED_GPIO   GPIO0
#define LED_RED_PIN    10U
#define LED_GREEN_GPIO GPIO0
#define LED_GREEN_PIN  27U
#define LED_BLUE_GPIO  GPIO1
#define LED_BLUE_PIN   2U

#define VIBRATION_GPIO GPIO0
#define VIBRATION_PIN  24U /* Arduino J2-D11 */
#define FAN_RELAY_GPIO GPIO0
#define FAN_RELAY_PIN  26U /* Arduino J2-D12 */
#define HAZARD_L_GPIO  GPIO0
#define HAZARD_L_PIN   25U /* Arduino J2-D13 */
#define HAZARD_R_GPIO  GPIO4
#define HAZARD_R_PIN   0U  /* Arduino J2-D18 */

/* ---- Driver / operator inputs — active-low (pulled up in pin_mux.c) ----- */
#define ACK_BUTTON_GPIO   GPIO4
#define ACK_BUTTON_PIN    1U /* Arduino J2-D19 */
#define REARM_BUTTON_GPIO GPIO2
#define REARM_BUTTON_PIN  3U /* Arduino J3-5 */
#define ESTOP_SENSE_GPIO  GPIO2
#define ESTOP_SENSE_PIN   5U /* Arduino J3-9, see pin_mux.c header comment */

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
