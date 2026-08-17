/*
 * Board-port macros for dms-sim-mcxn947 (FRDM-MCXN947 / hardware CAN test
 * node). See ../pin_mux.c for the per-pin cross-reference.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _APP_H_
#define _APP_H_

#include "fsl_gpio.h"

/*${macro:start}*/

/* ---- CAN0 (FLEXCAN0) -- same peripheral/pins as vcs-mcxn947 ------------- */
#define CAN_LINK_BASEADDR CAN0
#define CAN_LINK_CLK_FREQ CLOCK_GetFlexcanClkFreq(0U)

/* ---- Status LED (active-low) -------------------------------------------- */
#define LED_RED_GPIO   GPIO0
#define LED_RED_PIN    10U
#define LED_GREEN_GPIO GPIO0
#define LED_GREEN_PIN  27U
#define LED_BLUE_GPIO  GPIO1
#define LED_BLUE_PIN   2U

/*${macro:end}*/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
/*${prototype:start}*/
void BOARD_InitHardware(void);
/*${prototype:end}*/

#endif /* _APP_H_ */
