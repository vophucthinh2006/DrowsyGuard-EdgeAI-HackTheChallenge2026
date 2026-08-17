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
 * match the spec byte-for-byte. Throttle is now a real input (2026-08-15):
 * the board's own onboard SW2/SW3 buttons act as gas/brake, ramping
 * dg_throttle_setpoint_t up/down each control tick (see ControlTask).
 * Current/voltage sensing is still not wired (OI-05-01), so those fault
 * paths are dormant until real hardware is measured.
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
#include "sim_uart/sim_uart.h"

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

/* Gas/brake pedal simulation (SW2/SW3, see app.h). Braking is deliberately
 * faster than accelerating (matches real-vehicle feel and the existing
 * MOTION_RAMP_PCT_PER_TICK=40 %/s safe-stop ramp's order of magnitude).
 * COAST (2026-08-16): releasing BOTH gas and brake is its own case, not
 * "hold the last setpoint" -- a real car with nobody on the pedals slows
 * down on its own (engine braking / rolling friction), it doesn't cruise
 * forever. Slower than an active brake press, faster than doing nothing. */
#define THROTTLE_MAX_PCT       (100.0f)
#define GAS_RAMP_PCT_PER_S     (20.0f)
#define BRAKE_RAMP_PCT_PER_S   (40.0f)
#define COAST_RAMP_PCT_PER_S   (10.0f)

typedef struct {
  GPIO_Type *gpio;
  uint32_t pin;
  bool activeLow;
  uint8_t sameCount;
  bool lastRaw;
  bool debounced;
} dg_debounced_input_t;

static dg_debounced_input_t s_gasButton   = {GAS_BUTTON_GPIO, GAS_BUTTON_PIN, true};
static dg_debounced_input_t s_brakeButton = {BRAKE_BUTTON_GPIO, BRAKE_BUTTON_PIN, true};

/* Persistent throttle setpoint the gas/brake buttons ramp up/down, [0,100],
 * always forward (no reverse pedal on this test rig). Lives across control
 * ticks (unlike the old bench-only fixed {40,40} value it replaces) so
 * holding gas actually accelerates and holding brake actually decelerates,
 * instead of snapping to a fixed duty the instant re-arm was held. */
static float s_throttleSetpointPct = 0.0f;

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
  bool prevGas   = false;
  bool prevBrake = false;

  PRINTF("[control] control_task started (100 Hz)\r\n");

  for (;;) {
    uint32_t now_ms = NowMs();

    bool gas   = DebounceUpdate(&s_gasButton);
    bool brake = DebounceUpdate(&s_brakeButton);

    /* Debug log on every press/release edge (debounced), not every tick --
     * added 2026-08-15 so pressing SW2/SW3 is visible on the console. */
    if (gas != prevGas) {
      PRINTF("[control] SW2 (gas) %s\r\n", gas ? "PRESSED" : "released");
    }
    if (brake != prevBrake) {
      PRINTF("[control] SW3 (brake) %s\r\n", brake ? "PRESSED" : "released");
    }
    prevGas   = gas;
    prevBrake = brake;

    /* Hold-to-drive throttle (VEH-011, extended 2026-08-16): three cases,
     * not two -- holding gas ramps up at GAS_RAMP_PCT_PER_S; holding brake
     * ramps down at BRAKE_RAMP_PCT_PER_S (fastest); releasing BOTH coasts
     * down at COAST_RAMP_PCT_PER_S instead of holding the last setpoint --
     * only an active gas press ever raises this value again, exactly like
     * letting off the pedal on a real car. Brake wins on a simultaneous
     * press with gas. */
    const float dt_s = (float)CONTROL_TICK_MS / 1000.0f;
    if (brake) {
      s_throttleSetpointPct -= BRAKE_RAMP_PCT_PER_S * dt_s;
    } else if (gas) {
      s_throttleSetpointPct += GAS_RAMP_PCT_PER_S * dt_s;
    } else {
      s_throttleSetpointPct -= COAST_RAMP_PCT_PER_S * dt_s;
    }
    if (s_throttleSetpointPct < 0.0f) {
      s_throttleSetpointPct = 0.0f;
    } else if (s_throttleSetpointPct > THROTTLE_MAX_PCT) {
      s_throttleSetpointPct = THROTTLE_MAX_PCT;
    }

    dg_safety_inputs_t inputs = {0};
    /* motor_current_ma / motor_rail_mv stay 0 = "not wired" per OI-05-01,
     * which safety.c's EvaluateFaults() treats as "no reading available",
     * never as an actual fault -- unrelated to gas/brake, still open. */
    /* throttle_nonzero reflects the real gas-pedal-derived setpoint. Must
     * NOT be gated on Safety_GetState() == kDgVehArmedIdle -- once RUN is
     * entered the state is no longer ArmedIdle, so that gate would flip
     * throttle_nonzero back to false on the very next tick even while gas
     * is still held, bouncing RUN -> ArmedIdle -> RUN every control tick
     * (10 ms) and pinning motor duty near 0 % forever. throttle_nonzero
     * should reflect only the raw "is throttle asserted" signal; which
     * *states* react to it is safety.c's business, not this input's. */
    inputs.throttle_nonzero = (s_throttleSetpointPct > 0.0f);

    CanLink_UpdateSupervisor(now_ms);
    Safety_Tick(now_ms, &inputs);

    dg_vehicle_state_t state = Safety_GetState();
    dg_link_state_t link     = CanLink_GetLinkState();

    /* Sticky speed cap (2026-08-16, explicit product requirement: "tuyệt
     * đối không quay lại tốc độ ban đầu khi hết cảnh báo"). Motion_Tick()'s
     * cap (VEH-020) is a live ceiling re-read every tick -- left alone, a
     * LIMITED-then-RUN round trip (alert fires, then clears) would ramp the
     * ACTUAL duty back up toward the still-high setpoint the instant the
     * cap loosens again, with no fresh gas press involved at all. Clamp the
     * persistent setpoint itself down whenever it exceeds the current cap,
     * so that recovery is permanent: once an alert has forced it down, only
     * the gas branch above can ever raise it again. Only meaningful while
     * actually driving (RUN/LIMITED) -- every other state already forces
     * actual duty to 0 in motion.c regardless of this value, and clamping
     * against the default state cap (0) here would zero out a setpoint
     * that's legitimately ramping up in ARMED_IDLE ahead of a throttle-
     * gated ArmedIdle -> RUN transition. */
    if (state == kDgVehRun || state == kDgVehLimited) {
      uint8_t cap = Motion_GetSpeedCap(state, link);
      if (s_throttleSetpointPct > (float)cap) {
        s_throttleSetpointPct = (float)cap;
      }
    }

    int8_t throttlePct = (int8_t)s_throttleSetpointPct; /* 0..100, always forward */
    dg_throttle_setpoint_t setpoint = {throttlePct, throttlePct};
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
  SimUart_Start(); /* bring-up aid, see sim_uart.h -- type "help" on the console */

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
