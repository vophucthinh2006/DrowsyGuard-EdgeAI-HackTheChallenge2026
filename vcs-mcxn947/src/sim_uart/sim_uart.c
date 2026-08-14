/*
 * DrowsyGuard VCS — UART CAN-frame simulator implementation.
 *
 * Two tasks:
 *   sim_uart_rx     — blocks (via 5 ms polling, DbgConsole_Getchar() is a
 *                      single non-blocking try, same pattern can_rx_task
 *                      already uses for its own mailbox poll) reading one
 *                      line at a time and dispatching commands.
 *   sim_uart_inject — every 100 ms, if not paused, re-injects the
 *                      last-commanded DMS_STATUS with a fresh seq, exactly
 *                      like a live DMS-AP would (spec 04: DMS_STATUS every
 *                      100 ms). Without this, a one-shot injected frame
 *                      would age past CAN_DEGRADE_MS/CAN_TIMEOUT_MS on its
 *                      own and the link would falsely show LINK_LOST a
 *                      second after every command.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "sim_uart.h"

#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "can_link.h"
#include "fsl_debug_console.h"
#include "icd.h"
#include "motion.h"
#include "safety.h"

#define SIM_UART_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE + 400U)
#define SIM_UART_LINE_MAX (48U)
#define SIM_INJECT_PERIOD_MS (100U) /* DMS_STATUS's real cadence, spec 04 */

typedef struct {
  bool paused;
  dg_alert_level_t level;
  bool calibDone;
  bool sensorLost;
  uint8_t seq;
  bool watch; /* auto-print a status line every SIM_INJECT_PERIOD_MS, see "watch" cmd */
} dg_sim_state_t;

/* Only sim_uart_rx writes paused/level/calibDone/sensorLost/watch; only
 * sim_uart_inject writes seq. Neither task locks against the other, same as
 * can_link.c's own cross-task state (see can_link.h "safe to call from any
 * task context" note) -- a torn read here at worst delays one 100 ms cycle
 * by picking up a half-updated command, which is not a correctness issue
 * for a debug console. */
static dg_sim_state_t s_sim = {.paused = true,
                                .level = kDgAlertL0Normal,
                                .calibDone = false,
                                .sensorLost = false,
                                .seq = 0U,
                                .watch = false};

static void PrintHelp(void) {
  PRINTF(
      "\r\n[sim] CAN frame simulator -- commands:\r\n"
      "  l0 | l1 | l2 | l3     set alert level, resume injecting at 100 ms\r\n"
      "  calib on|off          set DMS_STATUS.flag_calib_done for future frames\r\n"
      "  sensorlost on|off     set DMS_STATUS.flag_sensor_lost for future frames\r\n"
      "  pause                 stop injecting -- link degrades then LINK_LOST, like a dead bus\r\n"
      "  resume                resume injecting the last level\r\n"
      "  estop 1|2|3|4         inject EMERGENCY_STOP (1=physical 2=dms 3=vcs 4=operator)\r\n"
      "  status                 print current vehicle_state/motion/link snapshot\r\n"
      "  watch on|off           auto-print a status line every 100 ms (no need to type status)\r\n"
      "  help                   this text\r\n");
}

/* Compact one-liner for "watch on" -- printed every SIM_INJECT_PERIOD_MS
 * from sim_uart_inject, so it stays terse instead of repeating PrintStatus()'s
 * multi-line block on every tick. */
static void PrintWatchLine(uint32_t now_ms) {
  static const char *const kStateNames[] = {"INIT",   "DISARMED", "ARMED_IDLE", "RUN",
                                             "LIMITED", "DECEL",    "STOPPED",    "LINK_LOST",
                                             "FAULT",   "ESTOP"};
  static const char *const kLinkNames[] = {"OK", "DEGRADED", "LOST"};

  dg_vehicle_state_t state  = Safety_GetState();
  dg_motion_status_t motion = Motion_GetStatus();
  dg_link_state_t link      = CanLink_GetLinkState();

  /* Plain \r\n, not an in-place \r overwrite: not every serial viewer (GUI
   * consoles especially) renders a bare carriage-return as "erase this
   * line" -- some just print it literally. Appending a new line every
   * 100 ms is more log noise but never garbles. */
  PRINTF("[watch] t=%ums state=%-10s link=%-8s dutyL=%3u%%(%s) dutyR=%3u%%(%s) cap=%u%%\r\n",
         (unsigned int)now_ms, kStateNames[(unsigned int)state], kLinkNames[(unsigned int)link],
         (unsigned int)motion.duty_left_pct, motion.dir_left_reverse ? "rev" : "fwd",
         (unsigned int)motion.duty_right_pct, motion.dir_right_reverse ? "rev" : "fwd",
         (unsigned int)motion.speed_cap_pct);
}

static void PrintStatus(void) {
  static const char *const kStateNames[] = {"INIT",   "DISARMED", "ARMED_IDLE", "RUN",
                                             "LIMITED", "DECEL",    "STOPPED",    "LINK_LOST",
                                             "FAULT",   "ESTOP"};
  static const char *const kLinkNames[] = {"OK", "DEGRADED", "LOST"};

  dg_vehicle_state_t state  = Safety_GetState();
  dg_fault_flags_t faults   = Safety_GetFaults();
  dg_motion_status_t motion = Motion_GetStatus();
  dg_link_state_t link      = CanLink_GetLinkState();

  PRINTF("\r\n[sim] state=%s link=%s cap=%u%% dutyL=%u%%(%s) dutyR=%u%%(%s)\r\n"
         "      faults: driver=%u wdt=%u can_timeout=%u undervolt=%u estop=%u\r\n"
         "      sim: paused=%u level=L%u calib=%u sensorlost=%u\r\n",
         kStateNames[(unsigned int)state], kLinkNames[(unsigned int)link],
         (unsigned int)motion.speed_cap_pct, (unsigned int)motion.duty_left_pct,
         motion.dir_left_reverse ? "rev" : "fwd", (unsigned int)motion.duty_right_pct,
         motion.dir_right_reverse ? "rev" : "fwd", (unsigned int)faults.fault_driver,
         (unsigned int)faults.fault_watchdog_reset, (unsigned int)faults.fault_can_timeout,
         (unsigned int)faults.fault_undervoltage, (unsigned int)faults.estop_active,
         (unsigned int)s_sim.paused, (unsigned int)s_sim.level, (unsigned int)s_sim.calibDone,
         (unsigned int)s_sim.sensorLost);
}

static void HandleLine(char *line) {
  size_t len = strlen(line);
  while (len > 0U && (line[len - 1U] == '\r' || line[len - 1U] == '\n' || line[len - 1U] == ' ')) {
    line[--len] = '\0';
  }
  if (len == 0U) {
    return;
  }

  if (strcmp(line, "l0") == 0) {
    s_sim.level = kDgAlertL0Normal;
    s_sim.paused = false;
    PRINTF("[sim] -> L0, injecting\r\n");
  } else if (strcmp(line, "l1") == 0) {
    s_sim.level = kDgAlertL1Early;
    s_sim.paused = false;
    PRINTF("[sim] -> L1, injecting\r\n");
  } else if (strcmp(line, "l2") == 0) {
    s_sim.level = kDgAlertL2Drowsy;
    s_sim.paused = false;
    PRINTF("[sim] -> L2, injecting\r\n");
  } else if (strcmp(line, "l3") == 0) {
    s_sim.level = kDgAlertL3Danger;
    s_sim.paused = false;
    PRINTF("[sim] -> L3, injecting\r\n");
  } else if (strcmp(line, "calib on") == 0) {
    s_sim.calibDone = true;
    PRINTF("[sim] calib_done = 1\r\n");
  } else if (strcmp(line, "calib off") == 0) {
    s_sim.calibDone = false;
    PRINTF("[sim] calib_done = 0\r\n");
  } else if (strcmp(line, "sensorlost on") == 0) {
    s_sim.sensorLost = true;
    PRINTF("[sim] sensor_lost = 1\r\n");
  } else if (strcmp(line, "sensorlost off") == 0) {
    s_sim.sensorLost = false;
    PRINTF("[sim] sensor_lost = 0\r\n");
  } else if (strcmp(line, "pause") == 0) {
    s_sim.paused = true;
    PRINTF("[sim] paused -- link will degrade then go LINK_LOST like a dead bus\r\n");
  } else if (strcmp(line, "resume") == 0) {
    s_sim.paused = false;
    PRINTF("[sim] resumed at L%u\r\n", (unsigned int)s_sim.level);
  } else if (strncmp(line, "estop ", 6U) == 0) {
    int reason = atoi(&line[6]);
    if (reason >= 1 && reason <= 4) {
      CanLink_SimInjectEmergencyStop((dg_estop_reason_t)reason);
      PRINTF("[sim] injected EMERGENCY_STOP reason=%d\r\n", reason);
    } else {
      PRINTF("[sim] usage: estop 1|2|3|4\r\n");
    }
  } else if (strcmp(line, "status") == 0) {
    PrintStatus();
  } else if (strcmp(line, "watch on") == 0) {
    s_sim.watch = true;
    PRINTF("[sim] watch on -- printing a status line every 100 ms\r\n");
  } else if (strcmp(line, "watch off") == 0) {
    s_sim.watch = false;
    PRINTF("[sim] watch off\r\n");
  } else if (strcmp(line, "help") == 0) {
    PrintHelp();
  } else {
    PRINTF("[sim] unknown command '%s' -- type 'help'\r\n", line);
  }
}

static void SimUartRxTask(void *pv) {
  (void)pv;
  char line[SIM_UART_LINE_MAX];
  size_t idx = 0U;

  PrintHelp();
  PRINTF("[sim] > ");

  for (;;) {
    int c = GETCHAR();
    if (c < 0) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    PUTCHAR(c); /* local echo -- most serial terminals don't echo for you */
    if (c == '\r' || c == '\n') {
      PRINTF("\r\n");
      line[idx] = '\0';
      HandleLine(line);
      idx = 0U;
      PRINTF("[sim] > ");
    } else if (idx + 1U < SIM_UART_LINE_MAX) {
      line[idx++] = (char)c;
    }
  }
}

static void SimUartInjectTask(void *pv) {
  (void)pv;
  for (;;) {
    if (!s_sim.paused) {
      dg_dms_status_t status = {0};
      status.alert_level      = s_sim.level;
      status.seq              = s_sim.seq;
      status.flag_calib_done  = s_sim.calibDone;
      status.flag_sensor_lost = s_sim.sensorLost;
      status.face_conf_pct    = 100U; /* not 255 ("no face"), so nothing else reads this as invalid */
      s_sim.seq               = (uint8_t)((s_sim.seq + 1U) & 0x0FU);
      CanLink_SimInjectDmsStatus(&status);
    }
    if (s_sim.watch) {
      /* Unconditional on s_sim.paused -- watching a LINK_LOST develop after
       * "pause" is exactly the point of this command. */
      PrintWatchLine(xTaskGetTickCount() * portTICK_PERIOD_MS);
    }
    vTaskDelay(pdMS_TO_TICKS(SIM_INJECT_PERIOD_MS));
  }
}

void SimUart_Start(void) {
  if (xTaskCreate(SimUartRxTask, "sim_uart_rx", SIM_UART_TASK_STACK_SIZE, NULL,
                   tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
    PRINTF("[sim] sim_uart_rx task creation failed!\r\n");
  }
  if (xTaskCreate(SimUartInjectTask, "sim_uart_inject", SIM_UART_TASK_STACK_SIZE, NULL,
                   tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
    PRINTF("[sim] sim_uart_inject task creation failed!\r\n");
  }
}
