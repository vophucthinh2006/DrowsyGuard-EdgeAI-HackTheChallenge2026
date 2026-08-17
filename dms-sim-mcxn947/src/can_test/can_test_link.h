/*
 * DrowsyGuard hardware CAN test node — CAN0 (FlexCAN) link, DMS-AP role.
 *
 * This is the actual FLEXCAN0 path (mailboxes, CRC, bus timing), unlike
 * vcs-mcxn947/src/sim_uart's "inject decoded state directly" shortcut. Flash
 * this onto a second FRDM-MCXN947, wire CAN_H/CAN_L/GND between the two
 * boards' J10 headers (120 ohm termination at both physical ends, CAN-002),
 * and it lets you validate the real vcs-mcxn947 FLEXCAN0 RX path end-to-end
 * with two physical boards -- see ../../README.md and
 * vcs-mcxn947/README.md "Next steps" step 4.
 *
 * Mailbox layout mirrors vcs-mcxn947/src/can_link/can_link.c with RX/TX
 * swapped:
 *   MB0  RX  0x200 VCS_STATUS
 *   MB1  RX  0x201 VCS_EVENT
 *   MB2  RX  0x080 EMERGENCY_STOP (from VCS)
 *   MB3  TX  0x100 DMS_STATUS
 *   MB4  TX  0x101 DMS_METRICS
 *   MB5  TX  0x080 EMERGENCY_STOP (DMS-initiated)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef DG_CAN_TEST_LINK_H_
#define DG_CAN_TEST_LINK_H_

#include <stdbool.h>
#include <stdint.h>

#include "icd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* FlexCAN0 init at 500 kbit/s (CAN-001..003), same CRC-8 self-test gate as
 * the real VCS firmware -- refuses to bring up the link if the two nodes
 * would silently disagree on CRC. Call once from main() before starting any
 * task. */
bool CanTestLink_Init(void);

/* Starts the FreeRTOS task that decodes inbound frames and PRINTFs each one
 * as it arrives (this is a bring-up tool -- seeing every RX frame live on
 * the console is the point, not just a periodic snapshot). */
void CanTestLink_StartRxTask(void);

/* Non-blocking, fire-and-forget, same reasoning as
 * CanLink_TransmitVcsStatus() in the real VCS firmware: a blocking send with
 * no ACKing peer on the bus never returns. */
bool CanTestLink_TransmitDmsStatus(const dg_dms_status_t *status);
bool CanTestLink_TransmitDmsMetrics(const dg_dms_metrics_t *metrics);
bool CanTestLink_TransmitEmergencyStop(dg_estop_reason_t reason);

bool CanTestLink_GetLatestVcsStatus(dg_vcs_status_t *out);
uint32_t CanTestLink_GetVcsStatusCount(void);
uint32_t CanTestLink_GetVcsStatusSeqGaps(void);
uint32_t CanTestLink_GetVcsEventCount(void);
uint32_t CanTestLink_GetMsSinceLastVcsStatus(uint32_t now_ms);

bool CanTestLink_EmergencyStopReceived(dg_estop_reason_t *reason);
void CanTestLink_ClearEmergencyStopReceived(void);

#ifdef __cplusplus
}
#endif

#endif /* DG_CAN_TEST_LINK_H_ */
