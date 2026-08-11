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

/* ---- Motor drive (PWM1 SM0/SM1) — specs/05 §2.2/VEH-001 ----------------- */
#define MOTION_PWM_BASEADDR    PWM1
#define MOTION_PWM_SRC_CLK_FREQ CLOCK_GetFreq(kCLOCK_BusClk)

#define MOTION_L_AIN1_GPIO GPIO0
#define MOTION_L_AIN1_PIN  29U /* Arduino J1-D2 */
#define MOTION_L_AIN2_GPIO GPIO1
#define MOTION_L_AIN2_PIN  23U /* Arduino J1-D3 */
#define MOTION_R_BIN1_GPIO GPIO0
#define MOTION_R_BIN1_PIN  30U /* Arduino J1-D4 */
#define MOTION_R_BIN2_GPIO GPIO0
#define MOTION_R_BIN2_PIN  31U /* Arduino J1-D7 */
#define MOTION_STBY_GPIO   GPIO0
#define MOTION_STBY_PIN    28U /* Arduino J2-D8, active-high driver enable */

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
