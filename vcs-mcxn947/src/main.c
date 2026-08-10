/*
 * DrowsyGuard VCS — Vehicle Control Simulator (FRDM-MCXN947).
 *
 * Wires together the modules described in
 * specs/05-vehicle-control-spec.md §8 (task table, VEH-060):
 *   - can_rx_task   (highest,   event-driven)  -- can_link.c
 *   - control_task  (highest-1, 100 Hz)         -- this file: safety + motion
 *   - alert_task    (medium,    100 Hz)         -- this file: alerts pattern engine
 *   - telemetry_task(low,       10 Hz)          -- this file: VCS_STATUS TX
 *
 * This is a bring-up build (specs/README.md "Rev 0.1, pre-hardware"): the
 * CAN link, safety state machine, motor PWM and alert patterns are real and
 * match the spec byte-for-byte, but there is no live throttle input yet
 * (dg_throttle_setpoint_t is a fixed test value below, see the TODO) and
 * current/voltage sensing is not wired (OI-05-01), so those fault paths
 * are dormant until real hardware is measured.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "FreeRTOS.h"
#include "task.h"

#include "app.h"
#include "board.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"

#include "alerts/alerts.h"
#include "can_link/can_link.h"
#include "icd/icd.h"
#include "motion/motion.h"
#include "safety/safety.h"

#define DG_FW_VERSION "0.1.0-bringup"
/* DEV-071 wants a git short SHA + dirty flag embedded at boot. Not wired
 * into this out-of-tree app's build yet -- build.sh does not inject one.
 * Until it does, every boot banner below is implicitly "+dirty" and no
 * benchmark in specs/08-benchmark-log.md may cite a run against it
 * (DEV-072). Tracked as a project open item, not a spec open item, since it
 * is tooling rather than firmware behaviour. */

#define CONTROL_TASK_PRIORITY (configMAX_PRIORITIES - 2)
#define ALERT_TASK_PRIORITY (configMAX_PRIORITIES - 3)
#define TELEMETRY_TASK_PRIORITY (configMAX_PRIORITIES - 4)
#define TASK_STACK_SIZE (configMINIMAL_STACK_SIZE + 512U)

#define CONTROL_TICK_MS (10U)  /* 100 Hz, VEH-060/061 */
#define ALERT_TICK_MS (10U)    /* 100 Hz, VEH-060 */
#define TELEMETRY_PERIOD_MS (100U) /* matches DMS_STATUS/VCS_STATUS cycle, spec 04 */

#define DEBOUNCE_SAMPLES (3U) /* 3 * 10 ms = 30 ms >= the >=20 ms in VEH-041 */

typedef struct {
  GPIO_Type *gpio;
  uint32_t pin;
  bool activeLow;
  uint8_t sameCount;
  bool lastRaw;
  bool debounced;
} dg_debounced_input_t;

static dg_debounced_input_t s_ackButton   = {ACK_BUTTON_GPIO, ACK_BUTTON_PIN, true};
static dg_debounced_input_t s_rearmButton = {REARM_BUTTON_GPIO, REARM_BUTTON_PIN, true};
static dg_debounced_input_t s_estopSense  = {ESTOP_SENSE_GPIO, ESTOP_SENSE_PIN, false};

static bool DebounceUpdate(dg_debounced_input_t *in) {
  bool raw = (GPIO_PinRead(in->gpio, in->pin) != 0U);
  bool asserted = in->activeLow ? !raw : raw;

  if (asserted == in->lastRaw) {
    if (in->sameCount < DEBOUNCE_SAMPLES) {
      in->sameCount++;
    }
  } else {
    in->sameCount = 0U;
    in->lastRaw    = asserted;
  }

  if (in->sameCount >= DEBOUNCE_SAMPLES) {
    in->debounced = asserted;
  }
  return in->debounced;
}

static uint32_t NowMs(void) { return xTaskGetTickCount() * portTICK_PERIOD_MS; }

static void ControlTask(void *pv) {
  (void)pv;
  bool prevAck   = false;
  bool prevRearm = false;

  PRINTF("[control] control_task started (100 Hz)\r\n");

  for (;;) {
    uint32_t now_ms = NowMs();

    bool ack   = DebounceUpdate(&s_ackButton);
    bool rearm = DebounceUpdate(&s_rearmButton);
    bool estop = DebounceUpdate(&s_estopSense);

    if (ack && !prevAck) {
      CanLink_RequestEvent(kDgEventAck);
    }
    if (rearm && !prevRearm) {
      CanLink_RequestEvent(kDgEventOperatorRearm);
    }
    prevAck   = ack;
    prevRearm = rearm;

    dg_safety_inputs_t inputs = {0};
    inputs.operator_rearm_pressed = rearm;
    inputs.ack_pressed            = ack;
    inputs.estop_sense_asserted   = estop;
    /* TODO: no physical throttle input wired yet -- ARMED_IDLE never sees
     * throttle_nonzero, so RUN is only reachable via the bench test hook
     * below until a real throttle/pedal signal exists (see README "What is
     * NOT wired yet"). motor_current_ma / motor_rail_mv stay 0 = "not
     * wired" per OI-05-01, which safety.c's EvaluateFaults() treats as "no
     * reading available", never as an actual fault. */
    inputs.throttle_nonzero = (Safety_GetState() == kDgVehArmedIdle) &&
                               rearm; /* bench-only: hold re-arm to roll */

    CanLink_UpdateSupervisor(now_ms);
    Safety_Tick(now_ms, &inputs);

    dg_vehicle_state_t state = Safety_GetState();
    dg_link_state_t link     = CanLink_GetLinkState();

    dg_throttle_setpoint_t setpoint = {40, 40}; /* bench-only fixed test value */
    Motion_Tick(CONTROL_TICK_MS, state, link, &setpoint);

    if (state == kDgVehDecel && Motion_SafeStopComplete()) {
      Safety_NotifySafeStopComplete();
    }

    CanLink_ServiceRepeaters(now_ms);
    Safety_ServiceWatchdog(); /* VEH-052: only after a full iteration completed */

    vTaskDelay(pdMS_TO_TICKS(CONTROL_TICK_MS));
  }
}

static void AlertTask(void *pv) {
  (void)pv;
  PRINTF("[alerts] alert_task started (100 Hz)\r\n");
  for (;;) {
    dg_dms_status_t dms;
    bool haveDms = CanLink_GetLatestDmsStatus(&dms);
    dg_alert_level_t level = haveDms ? dms.alert_level : kDgAlertL0Normal;
    bool sensorLost         = haveDms && dms.flag_sensor_lost;

    Alerts_Tick(ALERT_TICK_MS, Safety_GetState(), level, sensorLost,
                CanLink_GetLinkState(), Safety_GetFaults());

    vTaskDelay(pdMS_TO_TICKS(ALERT_TICK_MS));
  }
}

static void TelemetryTask(void *pv) {
  (void)pv;
  uint8_t seq = 0U;
  PRINTF("[telemetry] telemetry_task started (10 Hz)\r\n");
  for (;;) {
    dg_motion_status_t motion = Motion_GetStatus();
    dg_fault_flags_t faults   = Safety_GetFaults();

    dg_vcs_status_t status = {0};
    status.vehicle_state      = Safety_GetState();
    status.seq                = seq++;
    status.speed_cap_pct      = motion.speed_cap_pct;
    status.duty_left_pct      = motion.duty_left_pct;
    status.dir_left_reverse   = motion.dir_left_reverse;
    status.duty_right_pct     = motion.duty_right_pct;
    status.dir_right_reverse  = motion.dir_right_reverse;
    status.fault_driver          = faults.fault_driver;
    status.fault_watchdog_reset  = Safety_WatchdogResetFlagActive(NowMs());
    status.fault_can_timeout     = faults.fault_can_timeout;
    status.fault_undervoltage    = faults.fault_undervoltage;
    status.estop_active          = faults.estop_active;
    status.indicator_active      = false; /* no simulated indicator input yet */
    status.indicator_dir         = 0U;
    status.uptime_s              = (uint16_t)(NowMs() / 1000U);

    (void)CanLink_TransmitVcsStatus(&status);

    vTaskDelay(pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
  }
}

int main(void) {
  BOARD_InitHardware();

  PRINTF("\r\n=== DrowsyGuard VCS -- FRDM-MCXN947 ===\r\n");
  PRINTF("Firmware: %s (git SHA embedding not yet wired, see main.c)\r\n", DG_FW_VERSION);
  PRINTF("MCUX SDK version: %s\r\n", MCUXSDK_VERSION_FULL_STR);

  uint32_t bootMs = xTaskGetTickCount() * portTICK_PERIOD_MS;

  if (!CanLink_Init()) {
    PRINTF("[main] CAN bring-up failed (CRC self-test) -- halting, will not arm\r\n");
    for (;;) {
    }
  }

  Safety_Init();
  Safety_CheckResetCause(bootMs);
  Motion_Init();
  Alerts_Init();

  CanLink_StartRxTask();

  if (xTaskCreate(ControlTask, "control_task", TASK_STACK_SIZE, NULL,
                   CONTROL_TASK_PRIORITY, NULL) != pdPASS) {
    PRINTF("control_task creation failed!\r\n");
    while (1) {
    }
  }
  if (xTaskCreate(AlertTask, "alert_task", TASK_STACK_SIZE, NULL, ALERT_TASK_PRIORITY,
                   NULL) != pdPASS) {
    PRINTF("alert_task creation failed!\r\n");
    while (1) {
    }
  }
  if (xTaskCreate(TelemetryTask, "telemetry_task", TASK_STACK_SIZE, NULL,
                   TELEMETRY_TASK_PRIORITY, NULL) != pdPASS) {
    PRINTF("telemetry_task creation failed!\r\n");
    while (1) {
    }
  }

  vTaskStartScheduler();

  /* Only returns on out-of-heap for the idle task. */
  for (;;) {
  }
}
