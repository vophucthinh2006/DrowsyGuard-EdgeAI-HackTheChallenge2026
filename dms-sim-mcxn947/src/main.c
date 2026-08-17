/*
 * DrowsyGuard hardware CAN test node ("dms-sim") — FRDM-MCXN947.
 *
 * Purpose: validate vcs-mcxn947's real FLEXCAN0 RX path (mailboxes, CRC, bus
 * timing) with two physical boards on a real CAN bus. Flash this onto a
 * *second* FRDM-MCXN947, wire CAN_H/CAN_L/GND between the two boards' J10
 * headers with 120 ohm termination at each physical bus end (CAN-002).
 *
 * Fully autonomous, no console/commands: starts transmitting a real
 * DMS_STATUS frame every 100 ms the instant it boots, and every
 * received VCS_STATUS/VCS_EVENT/EMERGENCY_STOP is printed live by
 * can_test_link.c's RX task. A one-line TX/RX packet-count summary prints
 * every 1 s so the counts are visible without typing anything.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "FreeRTOS.h"
#include "task.h"

#include "app.h"
#include "board.h"
#include "fsl_debug_console.h"

#include "can_test/can_test_link.h"

#define DMS_SIM_FW_VERSION "0.2.0-test-node"

#define TX_PERIOD_MS (100U)      /* DMS_STATUS real cadence, spec 04 */
#define SUMMARY_PERIOD_TICKS (10U) /* 10 * 100 ms = 1 s */

static void TxCountTask(void *pv) {
  (void)pv;
  uint8_t seq = 0U;
  uint32_t txCount = 0U;
  uint32_t tick = 0U;

  for (;;) {
    dg_dms_status_t status = {0};
    status.alert_level   = kDgAlertL0Normal;
    status.seq           = seq;
    status.face_conf_pct = 100U; /* not 255 ("no face") */
    seq = (uint8_t)((seq + 1U) & 0x0FU);

    if (CanTestLink_TransmitDmsStatus(&status)) {
      txCount++;
    }

    tick++;
    if ((tick % SUMMARY_PERIOD_TICKS) == 0U) {
      uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
      PRINTF("[count] tx_dms_status=%u rx_vcs_status=%u rx_vcs_status_seq_gaps=%u "
             "rx_vcs_event=%u ms_since_last_rx=%u\r\n",
             (unsigned int)txCount, (unsigned int)CanTestLink_GetVcsStatusCount(),
             (unsigned int)CanTestLink_GetVcsStatusSeqGaps(),
             (unsigned int)CanTestLink_GetVcsEventCount(),
             (unsigned int)CanTestLink_GetMsSinceLastVcsStatus(now_ms));
    }

    vTaskDelay(pdMS_TO_TICKS(TX_PERIOD_MS));
  }
}

int main(void) {
  BOARD_InitHardware();

  PRINTF("\r\n=== DrowsyGuard hardware CAN test node (dms-sim) -- FRDM-MCXN947 ===\r\n");
  PRINTF("Firmware: %s (autonomous, no console)\r\n", DMS_SIM_FW_VERSION);
  PRINTF("MCUX SDK version: %s\r\n", MCUXSDK_VERSION_FULL_STR);
  PRINTF("Role: DMS-AP stand-in. Wire CAN_H/CAN_L/GND (J10) to a vcs-mcxn947 board,\r\n"
         "      120-ohm termination at each bus end. Transmitting DMS_STATUS every\r\n"
         "      %u ms starting now -- no command needed.\r\n", (unsigned int)TX_PERIOD_MS);

  if (!CanTestLink_Init()) {
    PRINTF("[main] CAN bring-up failed (CRC self-test) -- halting\r\n");
    for (;;) {
    }
  }

  CanTestLink_StartRxTask();

  if (xTaskCreate(TxCountTask, "tx_count", configMINIMAL_STACK_SIZE + 400U, NULL,
                   tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
    PRINTF("[main] tx_count task creation failed!\r\n");
    for (;;) {
    }
  }

  vTaskStartScheduler();

  /* Only returns on out-of-heap for the idle task. */
  for (;;) {
  }
}
