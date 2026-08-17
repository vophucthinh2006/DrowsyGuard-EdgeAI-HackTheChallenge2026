/*
 * DrowsyGuard hardware CAN test node — CAN0 (FlexCAN) link implementation.
 *
 * Same peripheral/pins as vcs-mcxn947: FLEXCAN0 on PORT1_10 (CAN0_TXD,
 * ALT11) / PORT1_11 (CAN0_RXD, ALT11), on-board TJA1057GTK/3Z transceiver,
 * J10 header. See ../../board_port/pin_mux.c.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "can_test_link.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app.h"
#include "fsl_debug_console.h"
#include "fsl_flexcan.h"

#include "crc8.h"

#define CAN_TEST_TASK_PRIORITY (configMAX_PRIORITIES - 1)
#define CAN_TEST_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE + 400U)

#define MB_RX_VCS_STATUS (0U)
#define MB_RX_VCS_EVENT  (1U)
#define MB_RX_ESTOP      (2U)
#define MB_TX_DMS_STATUS  (3U)
#define MB_TX_DMS_METRICS (4U)
#define MB_TX_ESTOP       (5U)

static flexcan_handle_t s_canHandle;

typedef struct {
  flexcan_frame_t frame;
  volatile bool pending;
  volatile uint32_t overrun_count;
} dg_rx_slot_t;

static dg_rx_slot_t s_rxSlot[3]; /* indexed by MB_RX_* */
static flexcan_mb_transfer_t s_rxXfer[3];

static dg_vcs_status_t s_latestVcsStatus;
static bool s_haveVcsStatus;
static int8_t s_lastVcsStatusSeq = -1;
static uint32_t s_lastVcsStatusRxTickMs;
static uint32_t s_vcsStatusCount;
static uint32_t s_vcsStatusSeqGaps;

static uint32_t s_vcsEventCount;

static dg_estop_reason_t s_estopReason;
static volatile bool s_estopReceived;

static uint32_t TicksToMs(uint32_t ticks) { return ticks * portTICK_PERIOD_MS; }

static FLEXCAN_CALLBACK(CanTestLink_Callback) {
  (void)base;
  (void)userData;

  if (status == kStatus_FLEXCAN_RxIdle) {
    if (result < 3U) {
      dg_rx_slot_t *slot = &s_rxSlot[result];
      if (slot->pending) {
        slot->overrun_count++;
      }
      slot->pending = true;
      (void)FLEXCAN_TransferReceiveNonBlocking(CAN_LINK_BASEADDR, handle,
                                                &s_rxXfer[result]);
    }
  }
  /* TX mailboxes: fire-and-forget, nothing to do on kStatus_FLEXCAN_TxIdle. */
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

bool CanTestLink_Init(void) {
  if (!DG_Crc8SelfTest()) {
    PRINTF("[can] CRC-8 self-test FAILED -- refusing to bring up the link\r\n");
    return false;
  }

  flexcan_config_t canConfig;
  FLEXCAN_GetDefaultConfig(&canConfig);
  canConfig.bitRate = 500000U; /* CAN-001, must match vcs-mcxn947 */

  flexcan_timing_config_t timing;
  memset(&timing, 0, sizeof(timing));
  if (FLEXCAN_CalculateImprovedTimingValues(CAN_LINK_BASEADDR, canConfig.bitRate,
                                             CAN_LINK_CLK_FREQ, &timing)) {
    memcpy(&canConfig.timingConfig, &timing, sizeof(timing));
  } else {
    PRINTF("[can] no improved timing solution found for %u bit/s at %u Hz\r\n",
           (unsigned int)canConfig.bitRate, (unsigned int)CAN_LINK_CLK_FREQ);
  }

  FLEXCAN_Init(CAN_LINK_BASEADDR, &canConfig, CAN_LINK_CLK_FREQ);
  FLEXCAN_TransferCreateHandle(CAN_LINK_BASEADDR, &s_canHandle, CanTestLink_Callback, NULL);

  FLEXCAN_SetRxMbGlobalMask(CAN_LINK_BASEADDR, FLEXCAN_RX_MB_STD_MASK(0x7FFU, 0, 0));

  SetupRxMailbox(MB_RX_VCS_STATUS, DG_CANID_VCS_STATUS);
  SetupRxMailbox(MB_RX_VCS_EVENT, DG_CANID_VCS_EVENT);
  SetupRxMailbox(MB_RX_ESTOP, DG_CANID_EMERGENCY_STOP);

  FLEXCAN_SetTxMbConfig(CAN_LINK_BASEADDR, MB_TX_DMS_STATUS, true);
  FLEXCAN_SetTxMbConfig(CAN_LINK_BASEADDR, MB_TX_DMS_METRICS, true);
  FLEXCAN_SetTxMbConfig(CAN_LINK_BASEADDR, MB_TX_ESTOP, true);

  PRINTF("[can] FlexCAN0 up: %u bit/s, presDiv=%u propSeg=%u pseg1=%u pseg2=%u rJumpwidth=%u\r\n",
         (unsigned int)canConfig.bitRate, (unsigned int)canConfig.timingConfig.preDivider,
         (unsigned int)canConfig.timingConfig.propSeg, (unsigned int)canConfig.timingConfig.phaseSeg1,
         (unsigned int)canConfig.timingConfig.phaseSeg2, (unsigned int)canConfig.timingConfig.rJumpwidth);
  return true;
}

static const char *const kVehStateNames[] = {"INIT",   "DISARMED", "ARMED_IDLE", "RUN",
                                              "LIMITED", "DECEL",    "STOPPED",    "LINK_LOST",
                                              "FAULT",   "ESTOP"};

static void ProcessVcsStatusSlot(uint32_t now_ms) {
  dg_rx_slot_t *slot = &s_rxSlot[MB_RX_VCS_STATUS];
  if (!slot->pending) {
    return;
  }
  slot->pending = false;

  uint8_t payload[8] = {
      slot->frame.dataByte0, slot->frame.dataByte1, slot->frame.dataByte2,
      slot->frame.dataByte3, slot->frame.dataByte4, slot->frame.dataByte5,
      slot->frame.dataByte6, slot->frame.dataByte7,
  };

  dg_vcs_status_t decoded;
  if (!DG_DecodeVcsStatus(payload, (uint8_t)slot->frame.length, &decoded)) {
    PRINTF("[rx] VCS_STATUS: DLC/CRC check FAILED (dlc=%u) -- discarded\r\n",
           (unsigned int)slot->frame.length);
    return;
  }

  if (s_lastVcsStatusSeq >= 0) {
    uint8_t expected = (uint8_t)((s_lastVcsStatusSeq + 1) & 0x0FU);
    if (decoded.seq != expected) {
      s_vcsStatusSeqGaps++;
    }
  }
  s_lastVcsStatusSeq = (int8_t)decoded.seq;

  s_latestVcsStatus       = decoded;
  s_haveVcsStatus          = true;
  s_lastVcsStatusRxTickMs  = now_ms;
  s_vcsStatusCount++;

  PRINTF("[rx] VCS_STATUS #%u seq=%u state=%s cap=%u%% dutyL=%u%%(%s) dutyR=%u%%(%s) "
         "faults[drv=%u wdt=%u can=%u uv=%u] estop=%u uptime=%us\r\n",
         (unsigned int)s_vcsStatusCount, (unsigned int)decoded.seq,
         kVehStateNames[(unsigned int)decoded.vehicle_state], (unsigned int)decoded.speed_cap_pct,
         (unsigned int)decoded.duty_left_pct, decoded.dir_left_reverse ? "rev" : "fwd",
         (unsigned int)decoded.duty_right_pct, decoded.dir_right_reverse ? "rev" : "fwd",
         (unsigned int)decoded.fault_driver, (unsigned int)decoded.fault_watchdog_reset,
         (unsigned int)decoded.fault_can_timeout, (unsigned int)decoded.fault_undervoltage,
         (unsigned int)decoded.estop_active, (unsigned int)decoded.uptime_s);
}

static void ProcessVcsEventSlot(void) {
  dg_rx_slot_t *slot = &s_rxSlot[MB_RX_VCS_EVENT];
  if (!slot->pending) {
    return;
  }
  slot->pending = false;

  uint8_t payload[2] = {slot->frame.dataByte0, slot->frame.dataByte1};
  dg_event_id_t eventId;
  uint8_t eventSeq;
  if (!DG_DecodeVcsEvent(payload, (uint8_t)slot->frame.length, &eventId, &eventSeq)) {
    PRINTF("[rx] VCS_EVENT: bad DLC (%u) -- discarded\r\n", (unsigned int)slot->frame.length);
    return;
  }

  s_vcsEventCount++;
  static const char *const kEventNames[] = {"?", "ACK", "OPERATOR_REARM", "ESTOP_ASSERTED",
                                             "ESTOP_RELEASED", "INDICATOR_ON", "INDICATOR_OFF"};
  unsigned int idx = (unsigned int)eventId;
  const char *name = (idx < (sizeof(kEventNames) / sizeof(kEventNames[0]))) ? kEventNames[idx] : "?";
  PRINTF("[rx] VCS_EVENT #%u id=%u(%s) seq=%u\r\n", (unsigned int)s_vcsEventCount,
         (unsigned int)eventId, name, (unsigned int)eventSeq);
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
    PRINTF("[rx] EMERGENCY_STOP reason=%u (from VCS)\r\n", (unsigned int)reason);
  } else {
    PRINTF("[rx] EMERGENCY_STOP: bad magic/DLC -- discarded (CAN-051)\r\n");
  }
}

static void CanTestLinkRxTask(void *pv) {
  (void)pv;
  PRINTF("[can] can_test_rx_task started\r\n");
  for (;;) {
    uint32_t now_ms = TicksToMs(xTaskGetTickCount());
    ProcessVcsStatusSlot(now_ms);
    ProcessVcsEventSlot();
    ProcessEstopSlot();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void CanTestLink_StartRxTask(void) {
  if (xTaskCreate(CanTestLinkRxTask, "can_test_rx", CAN_TEST_TASK_STACK_SIZE, NULL,
                   CAN_TEST_TASK_PRIORITY, NULL) != pdPASS) {
    PRINTF("can_test_rx_task creation failed!\r\n");
    while (1) {
    }
  }
}

bool CanTestLink_GetLatestVcsStatus(dg_vcs_status_t *out) {
  if (!s_haveVcsStatus) {
    return false;
  }
  *out = s_latestVcsStatus;
  return true;
}

uint32_t CanTestLink_GetVcsStatusCount(void) { return s_vcsStatusCount; }
uint32_t CanTestLink_GetVcsStatusSeqGaps(void) { return s_vcsStatusSeqGaps; }
uint32_t CanTestLink_GetVcsEventCount(void) { return s_vcsEventCount; }

uint32_t CanTestLink_GetMsSinceLastVcsStatus(uint32_t now_ms) {
  if (!s_haveVcsStatus) {
    return 0xFFFFFFFFU;
  }
  return now_ms - s_lastVcsStatusRxTickMs;
}

bool CanTestLink_EmergencyStopReceived(dg_estop_reason_t *reason) {
  if (!s_estopReceived) {
    return false;
  }
  *reason = s_estopReason;
  return true;
}

void CanTestLink_ClearEmergencyStopReceived(void) { s_estopReceived = false; }

/* ---- transmit path --------------------------------------------------------- */

static bool SendFrame(uint32_t mbIdx, uint32_t canId, const uint8_t *payload, uint8_t dlc) {
  flexcan_frame_t frame = {0};
  frame.id     = FLEXCAN_ID_STD(canId);
  frame.format = (uint8_t)kFLEXCAN_FrameFormatStandard;
  frame.type   = (uint8_t)kFLEXCAN_FrameTypeData;
  frame.length = dlc;
  frame.dataByte0 = payload[0];
  frame.dataByte1 = (dlc > 1U) ? payload[1] : 0U;
  frame.dataByte2 = (dlc > 2U) ? payload[2] : 0U;
  frame.dataByte3 = (dlc > 3U) ? payload[3] : 0U;
  frame.dataByte4 = (dlc > 4U) ? payload[4] : 0U;
  frame.dataByte5 = (dlc > 5U) ? payload[5] : 0U;
  frame.dataByte6 = (dlc > 6U) ? payload[6] : 0U;
  frame.dataByte7 = (dlc > 7U) ? payload[7] : 0U;

  /* Non-blocking, same reasoning as CanLink_TransmitVcsStatus() in the real
   * VCS firmware -- no infinite-timeout blocking send on a bus that might
   * have no live peer yet. */
  flexcan_mb_transfer_t xfer;
  xfer.mbIdx = (uint8_t)mbIdx;
  xfer.frame = &frame;
  return FLEXCAN_TransferSendNonBlocking(CAN_LINK_BASEADDR, &s_canHandle, &xfer) ==
         kStatus_Success;
}

bool CanTestLink_TransmitDmsStatus(const dg_dms_status_t *status) {
  uint8_t payload[8];
  uint8_t dlc = DG_EncodeDmsStatus(status, payload);
  return SendFrame(MB_TX_DMS_STATUS, DG_CANID_DMS_STATUS, payload, dlc);
}

bool CanTestLink_TransmitDmsMetrics(const dg_dms_metrics_t *metrics) {
  uint8_t payload[8];
  uint8_t dlc = DG_EncodeDmsMetrics(metrics, payload);
  return SendFrame(MB_TX_DMS_METRICS, DG_CANID_DMS_METRICS, payload, dlc);
}

bool CanTestLink_TransmitEmergencyStop(dg_estop_reason_t reason) {
  uint8_t payload[2];
  uint8_t dlc = DG_EncodeEmergencyStop(reason, payload);
  return SendFrame(MB_TX_ESTOP, DG_CANID_EMERGENCY_STOP, payload, dlc);
}
