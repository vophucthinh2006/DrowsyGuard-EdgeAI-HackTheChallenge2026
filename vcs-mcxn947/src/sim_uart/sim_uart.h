/*
 * DrowsyGuard VCS — UART CAN-frame simulator (bring-up/debug aid, NOT part
 * of specs/05-vehicle-control-spec.md VEH-060's normative task table).
 *
 * Reads line commands off the same LPUART4 debug console already used for
 * PRINTF (115200 8N1) and turns them into synthetic DMS_STATUS /
 * EMERGENCY_STOP "arrivals" via CanLink_SimInject*() (see can_link.h), so
 * the whole safety/motion/alerts chain can be exercised from a serial
 * terminal alone — no second CAN node, no DMS-AP, no camera. Complements,
 * but does not replace, `tools/can_inject.py` (specs/06 §3.2): that tool
 * exercises the real FLEXCAN0 RX path (mailboxes, CRC, bus timing); this
 * module bypasses it entirely and pokes decoded state directly.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef DG_SIM_UART_H_
#define DG_SIM_UART_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the 2 FreeRTOS tasks (line-reader + 100 ms periodic re-injector,
 * matching DMS_STATUS's real cadence per spec 04). Call once from main()
 * after CanLink_Init(). Injection starts PAUSED — nothing is sent until a
 * `l0`..`l3` command is typed, so a freshly booted board with no console
 * input still correctly reports LINK_LOST, matching real bring-up with no
 * DMS-AP connected. Type `help` on the console for the command list. */
void SimUart_Start(void);

#ifdef __cplusplus
}
#endif

#endif /* DG_SIM_UART_H_ */
