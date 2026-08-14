/*
 * DrowsyGuard VCS — CAN0 (FlexCAN) link.
 *
 * Owns the physical bus: init, the 3 RX mailboxes (DMS_STATUS, DMS_METRICS,
 * EMERGENCY_STOP), the 3 TX mailboxes (VCS_STATUS, VCS_EVENT,
 * EMERGENCY_STOP), CRC/DLC validation, and the link-timeout supervisor from
 * specs/04-interface-control-document.md §8. Everything above this module
 * (safety.c, control_task) only ever sees the decoded dg_dms_status_t and a
 * link-state enum — never a raw frame (CAN-013).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef DG_CAN_LINK_H_
#define DG_CAN_LINK_H_

#include <stdbool.h>
#include <stdint.h>

#include "icd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  kDgLinkOk = 0,       /* valid DMS_STATUS within CAN_DEGRADE_MS */
  kDgLinkDegraded = 1, /* > 300 ms since last valid frame (CAN-061) */
  kDgLinkLost = 2,     /* > 1000 ms since last valid frame (CAN-062) */
} dg_link_state_t;

/* FlexCAN0 init at 500 kbit/s (CAN-001..003), mailbox setup, self-test of
 * the CRC implementation (refuses to report ready if DG_Crc8SelfTest()
 * fails). Call once from main() before starting any task. */
bool CanLink_Init(void);

/* Starts the FreeRTOS task that decodes inbound frames posted by the FlexCAN
 * ISR callback (can_rx_task, spec 05 VEH-060, highest priority). */
void CanLink_StartRxTask(void);

/* Copies the most recently *validated* DMS_STATUS into *out. Returns false
 * if no valid frame has ever been received. This is the only way anything
 * outside can_link.c sees DMS_STATUS content (CAN-013). */
bool CanLink_GetLatestDmsStatus(dg_dms_status_t *out);
bool CanLink_GetLatestDmsMetrics(dg_dms_metrics_t *out);

/* Timeout supervisor (CAN-060..066). Call every control tick (10 ms) BEFORE
 * reading the link state — this is what advances LINK_LOST/degrade timing;
 * it does not depend on a frame having just arrived. */
void CanLink_UpdateSupervisor(uint32_t now_ms);
dg_link_state_t CanLink_GetLinkState(void);
uint32_t CanLink_GetFramesLostCount(void);
uint32_t CanLink_GetMsSinceLastValidFrame(uint32_t now_ms);

/* Transmits 0x200 VCS_STATUS immediately (telemetry_task, 100 ms cadence). */
bool CanLink_TransmitVcsStatus(const dg_vcs_status_t *status);

/* Queues 0x201 VCS_EVENT / 0x080 EMERGENCY_STOP for triple transmission at
 * 10 ms spacing (CAN-040 / CAN §7). Non-blocking: returns immediately, the
 * repeater is advanced by CanLink_ServiceRepeaters(). A second request of
 * the same kind before the first finishes replaces it (last request wins,
 * with a fresh event_seq) — acceptable because ACK/re-arm/e-stop-release are
 * not expected to be pressed faster than 30 ms apart. */
void CanLink_RequestEvent(dg_event_id_t event_id);
void CanLink_RequestEmergencyStop(dg_estop_reason_t reason);

/* Advances the event/e-stop repeaters. Call every control tick (10 ms). */
void CanLink_ServiceRepeaters(uint32_t now_ms);

/* True once an EMERGENCY_STOP frame with a valid magic byte has been
 * received from the bus (either direction, CAN-050) since the last time
 * CanLink_ClearEmergencyStopReceived() was called. safety.c polls this each
 * tick rather than the module pushing a callback into safety.c, to keep the
 * dependency direction can_link -> (nothing) instead of a cross-include. */
bool CanLink_EmergencyStopReceived(dg_estop_reason_t *reason);
void CanLink_ClearEmergencyStopReceived(void);

/* ---- bring-up / test injection (specs/06 §3.2 "CAN frame injection") -----
 * Feeds a synthetic frame into exactly the same state a real, CRC-valid CAN
 * RX would populate, so the safety FSM / motion / alerts chain can be
 * exercised without a second CAN node. Used by sim_uart.c. Unlike
 * `tools/can_inject.py` (spec 06), this does NOT exercise FLEXCAN0 itself —
 * no mailbox, no CRC, no bit timing — it is a firmware-side shortcut for
 * benches with only this one board and a serial cable, not a substitute for
 * validating the physical CAN path. Safe to call from any task context:
 * writes here follow the same unlocked pattern can_rx_task already uses, so
 * nothing new is introduced. Do not call this while a real DMS-AP is also
 * transmitting — whichever write lands last wins, same as two senders on
 * one ID on a real bus, which is exactly why you would not do that either. */
void CanLink_SimInjectDmsStatus(const dg_dms_status_t *status);
void CanLink_SimInjectEmergencyStop(dg_estop_reason_t reason);

#ifdef __cplusplus
}
#endif

#endif /* DG_CAN_LINK_H_ */
