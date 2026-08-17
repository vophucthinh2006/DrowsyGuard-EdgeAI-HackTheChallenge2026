/*
 * DrowsyGuard VCS — CAN0 (FlexCAN) link implementation.
 *
 * Peripheral and pins are the ones this board actually has wired to an
 * on-board TJA1057GTK/3Z transceiver and the J10 header: FLEXCAN0 on
 * PORT1_10 (CAN0_TXD, ALT11) / PORT1_11 (CAN0_RXD, ALT11) — confirmed
 * against the FRDM-MCXN947 schematic (UM12018 "FlexCAN interface schematic")
 * and cross-checked against the SDK's own
 * examples/_boards/frdmmcxn947/driver_examples/flexcan/interrupt_transfer
 * pin_mux.c, which configures the identical two pins the same way. This
 * closes specs/04-interface-control-document.md OI-04-01 for the VCS side:
 * no external transceiver is needed, the board already has one.
 *
 * Mailbox layout (6 of the 16 available, exact-ID match on all of them via
 * one global mask — every ID this project uses needs an exact match, so a
 * single FLEXCAN_SetRxMbGlobalMask(0x7FF) covers all three RX mailboxes):
 *   MB0  RX  0x100 DMS_STATUS
 *   MB1  RX  0x101 DMS_METRICS
 *   MB2  RX  0x080 EMERGENCY_STOP
 *   MB3  TX  0x200 VCS_STATUS
 *   MB4  TX  0x201 VCS_EVENT
 *   MB5  TX  0x080 EMERGENCY_STOP (VCS-initiated)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "can_link.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app.h"
#include "fsl_debug_console.h"
#include "fsl_flexcan.h"

#include "crc8.h"

#define CAN_LINK_TASK_PRIORITY (configMAX_PRIORITIES - 1) /* highest, VEH-060 */
#define CAN_LINK_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE + 400U)

#define MB_RX_DMS_STATUS  (0U)
#define MB_RX_DMS_METRICS (1U)
#define MB_RX_ESTOP       (2U)
#define MB_TX_VCS_STATUS  (3U)
#define MB_TX_VCS_EVENT   (4U)
#define MB_TX_ESTOP       (5U)

/* specs/04 §8 NORMATIVE */
#define CAN_DEGRADE_MS (300U)
#define CAN_TIMEOUT_MS (1000U)
/* specs/04 CAN-040 */
#define EVENT_REPEAT_COUNT (3U)
#define EVENT_REPEAT_SPACING_MS (10U)

static flexcan_handle_t s_canHandle;

/* Raw inbox, written only by the ISR callback, read only by can_rx_task.
 * Each slot is a single-frame "latest wins" buffer -- correct for periodic
 * status traffic where only the newest value matters (an overrun just means
 * the task was a tick late, counted, not fatal). */
typedef struct {
  flexcan_frame_t frame;
  volatile bool pending;
  volatile uint32_t overrun_count;
} dg_rx_slot_t;

static dg_rx_slot_t s_rxSlot[3]; /* indexed by MB_RX_* */
static flexcan_mb_transfer_t s_rxXfer[3];

/* Validated, decoded state -- this is what the rest of the firmware reads. */
static dg_dms_status_t s_latestDmsStatus;
static bool s_haveDmsStatus;
static int8_t s_lastDmsStatusSeq = -1; /* -1 = "no previous frame yet" */
static int8_t s_lastLoggedAlertLevel = -1; /* -1 = "never logged yet" */
static uint32_t s_lastValidRxTickMs;
static uint32_t s_framesLostCount;

static dg_dms_metrics_t s_latestDmsMetrics;
static bool s_haveDmsMetrics;

static dg_estop_reason_t s_estopReason;
static volatile bool s_estopReceived;

/* Event / e-stop triple-send repeaters (CAN-040, CAN §7). */
typedef struct {
  bool active;
  uint32_t mbIdx;
  uint32_t canId;
  uint8_t payload[8];
  uint8_t dlc;
  uint32_t sendsRemaining;
  uint32_t nextDueMs;
} dg_repeater_t;

static dg_repeater_t s_eventRepeater;
static dg_repeater_t s_estopRepeater;
static uint8_t s_eventSeq;

static uint32_t TicksToMs(uint32_t ticks) { return ticks * portTICK_PERIOD_MS; }

/* ---- ISR callback --------------------------------------------------------
 * Kept deliberately small (DEV-025): copy the frame out, set a flag, re-arm
 * the same mailbox. No decode, no CRC check, no PRINTF here -- all of that
 * happens in can_rx_task, which is where "kStatus_FLEXCAN_RxIdle" work
 * belongs once you are off the interrupt stack. */
static FLEXCAN_CALLBACK(CanLink_Callback) {
  (void)base;
  (void)userData;

  if (status == kStatus_FLEXCAN_RxIdle) {
    if (result < 3U) {
      dg_rx_slot_t *slot = &s_rxSlot[result];
      if (slot->pending) {
        slot->overrun_count++;
      }
      slot->pending = true;
      /* Re-arm immediately so the next frame on this mailbox is not missed
       * while can_rx_task is catching up. */
      (void)FLEXCAN_TransferReceiveNonBlocking(CAN_LINK_BASEADDR, handle,
                                                &s_rxXfer[result]);
    }
  } else if (status == kStatus_FLEXCAN_TxIdle) {
    /* Nothing to do: TX mailboxes are fire-and-forget from this module's
     * point of view, the repeaters track their own timing independently. */
  }
}

static void SetupRxMailbox(uint32_t mbIdx, uint32_t canId) {
  flexcan_rx_mb_config_t mbConfig;
  mbConfig.format = kFLEXCAN_FrameFormatStandard;
  mbConfig.type   = kFLEXCAN_FrameTypeData;
  mbConfig.id     = FLEXCAN_ID_STD(canId);
  FLEXCAN_SetRxMbConfig(CAN_LINK_BASEADDR, (uint8_t)mbIdx, &mbConfig, true);

  s_rxXfer[mbIdx].mbIdx = (uint8_t)mbIdx;
  s_rxXfer[mbIdx].frame = &s_rxSlot[mbIdx].frame;
  (void)FLEXCAN_TransferReceiveNonBlocking(CAN_LINK_BASEADDR, &s_canHandle,
                                            &s_rxXfer[mbIdx]);
}

bool CanLink_Init(void) {
  if (!DG_Crc8SelfTest()) {
    PRINTF("[can] CRC-8 self-test FAILED -- refusing to bring up the link\r\n");
    return false;
  }

  flexcan_config_t canConfig;
  FLEXCAN_GetDefaultConfig(&canConfig);
  canConfig.bitRate = 500000U; /* CAN-001 */

  flexcan_timing_config_t timing;
  memset(&timing, 0, sizeof(timing));
  if (FLEXCAN_CalculateImprovedTimingValues(CAN_LINK_BASEADDR, canConfig.bitRate,
                                             CAN_LINK_CLK_FREQ, &timing)) {
    memcpy(&canConfig.timingConfig, &timing, sizeof(timing));
  } else {
    PRINTF("[can] no improved timing solution found for %u bit/s at %u Hz -- "
           "using default quantum (verify bit time on a scope, CAN-070/TC-CAN-003)\r\n",
           (unsigned int)canConfig.bitRate, (unsigned int)CAN_LINK_CLK_FREQ);
  }

  FLEXCAN_Init(CAN_LINK_BASEADDR, &canConfig, CAN_LINK_CLK_FREQ);
  FLEXCAN_TransferCreateHandle(CAN_LINK_BASEADDR, &s_canHandle, CanLink_Callback, NULL);

  /* Every RX ID here needs an exact match, so one global exact-match mask
   * covers all three RX mailboxes (0x7FF = compare all 11 ID bits). */
  FLEXCAN_SetRxMbGlobalMask(CAN_LINK_BASEADDR, FLEXCAN_RX_MB_STD_MASK(0x7FFU, 0, 0));

  SetupRxMailbox(MB_RX_DMS_STATUS, DG_CANID_DMS_STATUS);
  SetupRxMailbox(MB_RX_DMS_METRICS, DG_CANID_DMS_METRICS);
  SetupRxMailbox(MB_RX_ESTOP, DG_CANID_EMERGENCY_STOP);

  FLEXCAN_SetTxMbConfig(CAN_LINK_BASEADDR, MB_TX_VCS_STATUS, true);
  FLEXCAN_SetTxMbConfig(CAN_LINK_BASEADDR, MB_TX_VCS_EVENT, true);
  FLEXCAN_SetTxMbConfig(CAN_LINK_BASEADDR, MB_TX_ESTOP, true);

  s_lastValidRxTickMs = TicksToMs(xTaskGetTickCount());

  PRINTF("[can] FlexCAN0 up: %u bit/s, presDiv=%u propSeg=%u pseg1=%u pseg2=%u rJumpwidth=%u\r\n",
         (unsigned int)canConfig.bitRate, (unsigned int)canConfig.timingConfig.preDivider,
         (unsigned int)canConfig.timingConfig.propSeg, (unsigned int)canConfig.timingConfig.phaseSeg1,
         (unsigned int)canConfig.timingConfig.phaseSeg2, (unsigned int)canConfig.timingConfig.rJumpwidth);
  return true;
}

/* ---- decode path (task context) ------------------------------------------ */

static void ProcessDmsStatusSlot(uint32_t now_ms) {
  dg_rx_slot_t *slot = &s_rxSlot[MB_RX_DMS_STATUS];
  if (!slot->pending) {
    return;
  }
  slot->pending = false;

  uint8_t payload[8];
  payload[0] = slot->frame.dataByte0;
  payload[1] = slot->frame.dataByte1;
  payload[2] = slot->frame.dataByte2;
  payload[3] = slot->frame.dataByte3;
  payload[4] = slot->frame.dataByte4;
  payload[5] = slot->frame.dataByte5;
  payload[6] = slot->frame.dataByte6;
  payload[7] = slot->frame.dataByte7;

  dg_dms_status_t decoded;
  if (!DG_DecodeDmsStatus(payload, (uint8_t)slot->frame.length, &decoded)) {
    /* CRC or DLC failure: discard, do NOT refresh the supervisor (CAN-011). */
    return;
  }

  /* Seq continuity is counted, not enforced -- a gap does not by itself
   * trigger the failsafe (CAN-012); only the elapsed-time supervisor does. */
  if (s_lastDmsStatusSeq >= 0) {
    uint8_t expected = (uint8_t)((s_lastDmsStatusSeq + 1) & 0x0FU);
    if (decoded.seq != expected) {
      s_framesLostCount++;
    }
  }
  s_lastDmsStatusSeq = (int8_t)decoded.seq;

  /* Log every time the alert level received from the DMS (UNO Q) changes --
   * added 2026-08-15, on-change only so it doesn't spam at the 100 ms
   * DMS_STATUS rate. s_lastLoggedAlertLevel starts at -1 ("never logged
   * yet") so the very first valid frame received also logs. */
  if ((int8_t)decoded.alert_level != s_lastLoggedAlertLevel) {
    static const char *const kAlertNames[] = {"L0_NORMAL", "L1_EARLY", "L2_DROWSY", "L3_DANGER"};
    PRINTF("[can] DMS alert_level -> %s (from UNO Q)\r\n",
           kAlertNames[(unsigned int)decoded.alert_level & 0x03U]);
    s_lastLoggedAlertLevel = (int8_t)decoded.alert_level;
  }

  s_latestDmsStatus = decoded;
  s_haveDmsStatus    = true;
  s_lastValidRxTickMs = now_ms;
}

static void ProcessDmsMetricsSlot(void) {
  dg_rx_slot_t *slot = &s_rxSlot[MB_RX_DMS_METRICS];
  if (!slot->pending) {
    return;
  }
  slot->pending = false;

  uint8_t payload[8] = {
      slot->frame.dataByte0, slot->frame.dataByte1, slot->frame.dataByte2,
      slot->frame.dataByte3, slot->frame.dataByte4, slot->frame.dataByte5,
      slot->frame.dataByte6, slot->frame.dataByte7,
  };

  dg_dms_metrics_t decoded;
  if (DG_DecodeDmsMetrics(payload, (uint8_t)slot->frame.length, &decoded)) {
    s_latestDmsMetrics = decoded;
    s_haveDmsMetrics    = true;
  }
  /* DMS_METRICS is telemetry-only (CAN-020): a bad frame is simply dropped,
   * it has no effect on the supervisor or on any actuation decision. */
}

static void ProcessEstopSlot(void) {
  dg_rx_slot_t *slot = &s_rxSlot[MB_RX_ESTOP];
  if (!slot->pending) {
    return;
  }
  slot->pending = false;

  uint8_t payload[2] = {slot->frame.dataByte0, slot->frame.dataByte1};
  dg_estop_reason_t reason;
  if (DG_DecodeEmergencyStop(payload, (uint8_t)slot->frame.length, &reason)) {
    s_estopReason   = reason;
    s_estopReceived = true;
  }
  /* A bad magic byte is exactly the case CAN-051 exists for: ignored. */
}

static dg_link_state_t s_lastLoggedLinkState = kDgLinkLost; /* matches boot default: no frame yet */

/* Debug: log every OK/DEGRADED/LOST transition -- added 2026-08-15. This is
 * the single most useful log for "why isn't the board being controlled":
 * if this never reaches OK, nothing downstream (alert_level, speed cap)
 * ever gets a chance to matter, regardless of what the UNO Q is sending. */
static void LogLinkStateChange(void) {
  dg_link_state_t link = CanLink_GetLinkState();
  if (link != s_lastLoggedLinkState) {
    static const char *const kLinkNames[] = {"OK", "DEGRADED", "LOST"};
    PRINTF("[can] link %s -> %s\r\n", kLinkNames[(unsigned int)s_lastLoggedLinkState],
           kLinkNames[(unsigned int)link]);
    s_lastLoggedLinkState = link;
  }
}

static void CanLinkRxTask(void *pv) {
  (void)pv;
  PRINTF("[can] can_rx_task started\r\n");
  for (;;) {
    /* Woken on every FlexCAN RX interrupt via the notification given in the
     * ISR callback path is intentionally NOT used here -- polling every
     * 5 ms is simpler, still comfortably inside the 100 ms DMS_STATUS
     * period, and avoids a notify-from-ISR race with three different
     * mailboxes feeding one task. */
    uint32_t now_ms = TicksToMs(xTaskGetTickCount());
    ProcessDmsStatusSlot(now_ms);
    ProcessDmsMetricsSlot();
    ProcessEstopSlot();
    LogLinkStateChange();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void CanLink_StartRxTask(void) {
  if (xTaskCreate(CanLinkRxTask, "can_rx_task", CAN_LINK_TASK_STACK_SIZE, NULL,
                   CAN_LINK_TASK_PRIORITY, NULL) != pdPASS) {
    PRINTF("can_rx_task creation failed!\r\n");
    while (1) {
    }
  }
}

bool CanLink_GetLatestDmsStatus(dg_dms_status_t *out) {
  if (!s_haveDmsStatus) {
    return false;
  }
  *out = s_latestDmsStatus;
  return true;
}

bool CanLink_GetLatestDmsMetrics(dg_dms_metrics_t *out) {
  if (!s_haveDmsMetrics) {
    return false;
  }
  *out = s_latestDmsMetrics;
  return true;
}

/* ---- timeout supervisor (CAN-060..066) ------------------------------------ */

void CanLink_UpdateSupervisor(uint32_t now_ms) {
  (void)now_ms; /* state is derived on read; nothing to advance here beyond
                 * what ProcessDmsStatusSlot() already does on arrival. Kept
                 * as an explicit call site per spec 05 VEH-050 so the
                 * control tick has one place that "runs" supervision. */
}

uint32_t CanLink_GetMsSinceLastValidFrame(uint32_t now_ms) {
  if (!s_haveDmsStatus) {
    return 0xFFFFFFFFU; /* "forever" until the first valid frame ever arrives */
  }
  return now_ms - s_lastValidRxTickMs;
}

dg_link_state_t CanLink_GetLinkState(void) {
  uint32_t now_ms = TicksToMs(xTaskGetTickCount());
  if (!s_haveDmsStatus) {
    return kDgLinkLost; /* CAN-063: absence is never L0 / never "ok" */
  }
  uint32_t elapsed = CanLink_GetMsSinceLastValidFrame(now_ms);
  if (elapsed >= CAN_TIMEOUT_MS) {
    return kDgLinkLost;
  }
  if (elapsed >= CAN_DEGRADE_MS) {
    return kDgLinkDegraded;
  }
  return kDgLinkOk;
}

uint32_t CanLink_GetFramesLostCount(void) { return s_framesLostCount; }

/* ---- transmit path --------------------------------------------------------- */

bool CanLink_TransmitVcsStatus(const dg_vcs_status_t *status) {
  flexcan_frame_t frame = {0};
  uint8_t payload[8];
  uint8_t dlc = DG_EncodeVcsStatus(status, payload);

  frame.id     = FLEXCAN_ID_STD(DG_CANID_VCS_STATUS);
  frame.format = (uint8_t)kFLEXCAN_FrameFormatStandard;
  frame.type   = (uint8_t)kFLEXCAN_FrameTypeData;
  frame.length = dlc;
  frame.dataByte0 = payload[0];
  frame.dataByte1 = payload[1];
  frame.dataByte2 = payload[2];
  frame.dataByte3 = payload[3];
  frame.dataByte4 = payload[4];
  frame.dataByte5 = payload[5];
  frame.dataByte6 = payload[6];
  frame.dataByte7 = payload[7];

  /* Non-blocking: on a bus with no ACKing peer (bring-up, single node) a
   * blocking send never returns -- FLEXCAN_TransferSendBlocking() polls with
   * no timeout in this build (FLEXCAN_POLLING_TIMEOUT=0) and would hang
   * telemetry_task, and every lower-priority task with it, forever. Firing
   * the frame and letting the ISR (kStatus_FLEXCAN_TxIdle in
   * CanLink_Callback) retire the mailbox costs nothing here: VCS_STATUS is
   * periodic latest-wins telemetry, so a frame dropped because the mailbox
   * was still busy from the previous cycle is simply superseded by the next
   * one 100 ms later. */
  flexcan_mb_transfer_t xfer;
  xfer.mbIdx = MB_TX_VCS_STATUS;
  xfer.frame = &frame;
  return FLEXCAN_TransferSendNonBlocking(CAN_LINK_BASEADDR, &s_canHandle, &xfer) ==
         kStatus_Success;
}

static void ArmRepeater(dg_repeater_t *rep, uint32_t mbIdx, uint32_t canId,
                         const uint8_t *payload, uint8_t dlc, uint32_t now_ms) {
  rep->active = true;
  rep->mbIdx  = mbIdx;
  rep->canId  = canId;
  memcpy(rep->payload, payload, dlc);
  rep->dlc            = dlc;
  rep->sendsRemaining = EVENT_REPEAT_COUNT;
  rep->nextDueMs       = now_ms; /* send the first copy immediately */
}

void CanLink_RequestEvent(dg_event_id_t event_id) {
  uint8_t payload[2];
  uint8_t seq = s_eventSeq++;
  uint8_t dlc = DG_EncodeVcsEvent(event_id, seq, payload);
  ArmRepeater(&s_eventRepeater, MB_TX_VCS_EVENT, DG_CANID_VCS_EVENT, payload, dlc,
              TicksToMs(xTaskGetTickCount()));
}

void CanLink_RequestEmergencyStop(dg_estop_reason_t reason) {
  uint8_t payload[2];
  uint8_t dlc = DG_EncodeEmergencyStop(reason, payload);
  ArmRepeater(&s_estopRepeater, MB_TX_ESTOP, DG_CANID_EMERGENCY_STOP, payload, dlc,
              TicksToMs(xTaskGetTickCount()));
}

static void ServiceOneRepeater(dg_repeater_t *rep, uint32_t now_ms) {
  if (!rep->active) {
    return;
  }
  if (now_ms < rep->nextDueMs) {
    return;
  }

  flexcan_frame_t frame = {0};
  frame.id     = FLEXCAN_ID_STD(rep->canId);
  frame.format = (uint8_t)kFLEXCAN_FrameFormatStandard;
  frame.type   = (uint8_t)kFLEXCAN_FrameTypeData;
  frame.length = rep->dlc;
  frame.dataByte0 = rep->payload[0];
  frame.dataByte1 = rep->payload[1];

  /* Non-blocking for the same reason as CanLink_TransmitVcsStatus(): this
   * runs from control_task (second-highest priority), and a blocking send
   * with no ACK on the bus would freeze it -- and with it every
   * lower-priority task (alerts, telemetry, the sim console), since a
   * priority-preempting busy-wait never yields. If the mailbox is still busy
   * finishing the previous copy (kStatus_FLEXCAN_TxBusy), leave the repeater
   * state untouched so this same copy is retried on the next control tick
   * (10 ms later) instead of being silently dropped. */
  flexcan_mb_transfer_t xfer;
  xfer.mbIdx = (uint8_t)rep->mbIdx;
  xfer.frame = &frame;
  if (FLEXCAN_TransferSendNonBlocking(CAN_LINK_BASEADDR, &s_canHandle, &xfer) !=
      kStatus_Success) {
    return; /* mailbox busy -- try again next tick, don't consume this copy */
  }

  rep->sendsRemaining--;
  rep->nextDueMs = now_ms + EVENT_REPEAT_SPACING_MS;
  if (rep->sendsRemaining == 0U) {
    rep->active = false;
  }
}

void CanLink_ServiceRepeaters(uint32_t now_ms) {
  ServiceOneRepeater(&s_eventRepeater, now_ms);
  ServiceOneRepeater(&s_estopRepeater, now_ms);
}

bool CanLink_EmergencyStopReceived(dg_estop_reason_t *reason) {
  if (!s_estopReceived) {
    return false;
  }
  *reason = s_estopReason;
  return true;
}

void CanLink_ClearEmergencyStopReceived(void) { s_estopReceived = false; }

/* ---- bring-up / test injection (see can_link.h) --------------------------- */

void CanLink_SimInjectDmsStatus(const dg_dms_status_t *status) {
  s_latestDmsStatus   = *status;
  s_haveDmsStatus      = true;
  s_lastDmsStatusSeq   = (int8_t)status->seq;
  s_lastValidRxTickMs  = TicksToMs(xTaskGetTickCount());
}

void CanLink_SimInjectEmergencyStop(dg_estop_reason_t reason) {
  s_estopReason   = reason;
  s_estopReceived = true;
}
