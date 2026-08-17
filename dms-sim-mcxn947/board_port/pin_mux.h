/*
 * Board-port pin muxing for dms-sim-mcxn947 (FRDM-MCXN947 / hardware CAN
 * test node).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _PIN_MUX_H_
#define _PIN_MUX_H_

#ifdef __cplusplus
extern "C" {
#endif

void BOARD_InitBootPins(void);
void BOARD_InitPins(void);

#ifdef __cplusplus
}
#endif

#endif /* _PIN_MUX_H_ */
