/*
 * DrowsyGuard VCS — safety state machine implementation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "safety.h"

#include "app.h"
#include "fsl_debug_console.h"
#include "fsl_wwdt.h"

/* specs/04 §8 / 05 §7 */
#define WATCHDOG_PERIOD_MS (500U)
/* VEH-053: fault_watchdog_reset stays set this long after a WDT reset. */
#define WATCHDOG_FLAG_DURATION_MS (5000U)
/* VEH-054/055 */
#define STALL_CURRENT_DEBOUNCE_MS (500U)
#define UNDERVOLT_DEBOUNCE_MS (200U)

static dg_vehicle_state_t s_state = kDgVehInit;
static dg_fault_flags_t s_faults;

static bool s_wdtResetAtBoot;
static uint32_t s_wdtFlagUntilMs;

static uint32_t s_overcurrentSinceMs; /* 0 = "not currently over limit" */
static uint32_t s_undervoltSinceMs;

/* Motion/alert modules read this indirectly through Safety_GetState(); the
 * safe-stop *sequencing itself* lives in motion.c (VEH-040..044) — safety.c
 * only decides WHEN to enter/leave DECEL/STOPPED, motion.c decides HOW the
 * ramp happens. */

static bool AlertLevelAtOrAbove(dg_alert_level_t level, dg_alert_level_t floor) {
  return (uint8_t)level >= (uint8_t)floor;
}

void Safety_Init(void) {
  wwdt_config_t config;
  WWDT_GetDefaultConfig(&config);
  config.enableWwdt   = true;
  config.windowValue  = 0xFFFFFFU; /* windowing disabled — see file header */
  uint32_t wdtFreq = CLOCK_GetWdtClkFreq(0U);
  config.timeoutValue = (wdtFreq / 1000U) * WATCHDOG_PERIOD_MS;
  WWDT_Init(WWDT, &config);

  s_state = kDgVehInit;
  PRINTF("[safety] WWDT armed at %u ms (clk=%u Hz, timeoutValue=%u)\r\n",
         (unsigned int)WATCHDOG_PERIOD_MS, (unsigned int)wdtFreq,
         (unsigned int)config.timeoutValue);
}

void Safety_CheckResetCause(uint32_t now_ms) {
  if (IS_WWDT_RESET) {
    s_wdtResetAtBoot = true;
    s_wdtFlagUntilMs = now_ms + WATCHDOG_FLAG_DURATION_MS;
    s_faults.fault_watchdog_reset = true;
    PRINTF("[safety] *** reset cause: WWDT0 timeout -- coming up DISARMED, "
           "not resuming (VEH-053) ***\r\n");
  }
}

bool Safety_WatchdogResetFlagActive(uint32_t now_ms) {
  if (!s_wdtResetAtBoot) {
    return false;
  }
  if (now_ms >= s_wdtFlagUntilMs) {
    s_faults.fault_watchdog_reset = false;
    return false;
  }
  return true;
}

void Safety_ServiceWatchdog(void) { WWDT_Refresh(WWDT); }

/* ---- fault evaluation (VEH-054/055) --------------------------------------- */

static void EvaluateFaults(uint32_t now_ms, const dg_safety_inputs_t *in) {
  /* OI-05-01: I_STALL_LIMIT / V_UNDERVOLT are not yet measured on real
   * hardware, so a current/voltage reading of exactly 0 means "not wired
   * yet" and must never be treated as a real over-limit condition — that
   * would put every unbuilt bring-up into a fault loop it can never clear.
   * Once the ADC path exists this guard is removed. */
  const bool currentSenseWired = (in->motor_current_ma != 0U);
  const bool railSenseWired    = (in->motor_rail_mv != 0U);

#define I_STALL_LIMIT_MA (1500U) /* placeholder, OI-05-01 */
#define V_UNDERVOLT_MV (5500U)   /* placeholder, OI-05-01 */

  if (currentSenseWired && in->motor_current_ma > I_STALL_LIMIT_MA) {
    if (s_overcurrentSinceMs == 0U) {
      s_overcurrentSinceMs = now_ms;
    } else if (now_ms - s_overcurrentSinceMs >= STALL_CURRENT_DEBOUNCE_MS) {
      s_faults.fault_driver = true;
    }
  } else {
    s_overcurrentSinceMs = 0U;
  }

  if (railSenseWired && in->motor_rail_mv < V_UNDERVOLT_MV) {
    if (s_undervoltSinceMs == 0U) {
      s_undervoltSinceMs = now_ms;
    } else if (now_ms - s_undervoltSinceMs >= UNDERVOLT_DEBOUNCE_MS) {
      s_faults.fault_undervoltage = true;
    }
  } else {
    s_undervoltSinceMs = 0U;
  }

  s_faults.estop_active = in->estop_sense_asserted;

  dg_estop_reason_t reason;
  if (CanLink_EmergencyStopReceived(&reason)) {
    s_faults.estop_active = true;
    CanLink_ClearEmergencyStopReceived();
  }

  dg_link_state_t link = CanLink_GetLinkState();
  s_faults.fault_can_timeout = (link == kDgLinkLost);
}

static bool AnyFaultActive(void) {
  return s_faults.fault_driver || s_faults.fault_can_timeout ||
         s_faults.fault_undervoltage;
  /* fault_watchdog_reset and estop_active are surfaced but handled by their
   * own explicit transitions below, not folded into the generic FAULT path
   * — an e-stop goes to ESTOP (recoverable by release+re-arm), a stale
   * watchdog flag alone does not re-trigger FAULT after the 5 s window. */
}

/* ---- state machine (VEH-010..014, DEV-027: one switch, default -> FAULT) - */

void Safety_Tick(uint32_t now_ms, const dg_safety_inputs_t *inputs) {
  EvaluateFaults(now_ms, inputs);

  dg_dms_status_t dms;
  bool haveDms      = CanLink_GetLatestDmsStatus(&dms);
  dg_link_state_t link = CanLink_GetLinkState();

  /* CAN-063: absence of a valid frame is NEVER L0. If we have never had a
   * valid frame, or the link is currently lost, treat the driver state as
   * unknown/worst-case for every decision below. */
  bool driverStateKnown = haveDms && (link != kDgLinkLost);
  dg_alert_level_t level = driverStateKnown ? dms.alert_level : kDgAlertL3Danger;
  bool calibDone          = haveDms && dms.flag_calib_done;

  /* E-stop pre-empts everything except INIT (still self-testing). */
  if (s_state != kDgVehInit &&
      (inputs->estop_sense_asserted || s_faults.estop_active)) {
    if (s_state != kDgVehEstop) {
      PRINTF("[safety] -> ESTOP\r\n");
    }
    s_state = kDgVehEstop;
  }

  switch (s_state) {
    case kDgVehInit:
      /* Self-test is "did CAN and PWM bring-up succeed" — checked once in
       * main() before the scheduler starts; by the time Safety_Tick() first
       * runs, INIT always advances immediately. */
      s_state = kDgVehDisarmed;
      break;

    case kDgVehDisarmed:
      if (AnyFaultActive()) {
        s_state = kDgVehFault;
        break;
      }
      /* VEH-011: needs both the operator re-arm button AND calib_done=1. */
      if (inputs->operator_rearm_pressed && calibDone) {
        s_state = kDgVehArmedIdle;
        PRINTF("[safety] armed\r\n");
      }
      break;

    case kDgVehArmedIdle:
      if (AnyFaultActive()) {
        s_state = kDgVehFault;
        break;
      }
      if (!calibDone) {
        s_state = kDgVehDisarmed;
        break;
      }
      if (inputs->throttle_nonzero) {
        s_state = kDgVehRun;
      }
      break;

    case kDgVehRun:
    case kDgVehLimited:
      if (AnyFaultActive()) {
        s_state = kDgVehFault;
        break;
      }
      if (level == kDgAlertL3Danger || link == kDgLinkLost) {
        s_state = kDgVehDecel; /* VEH-040, CAN-062 */
        PRINTF("[safety] -> DECEL (level=%u link=%u)\r\n", (unsigned int)level,
               (unsigned int)link);
        break;
      }
      if (level == kDgAlertL1Early || level == kDgAlertL2Drowsy ||
          link == kDgLinkDegraded) {
        s_state = kDgVehLimited;
      } else {
        s_state = kDgVehRun;
      }
      if (!inputs->throttle_nonzero && s_state != kDgVehLimited) {
        s_state = kDgVehArmedIdle;
      }
      break;

    case kDgVehDecel:
      /* motion.c owns ramp/brake timing (VEH-040..044) and reports
       * completion the same way everything else reports status: through
       * dg_vcs_status_t built in control_task. safety.c advances to
       * STOPPED once motion.c's safe-stop sequence is done — queried via
       * Motion_SafeStopComplete() from control_task, not duplicated here.
       * See control_task.c for the exact call sequence. */
      break;

    case kDgVehStopped:
      /* VEH-012/SYS-FR-033: level alone can never leave STOPPED. */
      if (inputs->operator_rearm_pressed) {
        s_state = kDgVehArmedIdle;
        s_faults = (dg_fault_flags_t){0};
        PRINTF("[safety] re-armed from STOPPED\r\n");
      }
      break;

    case kDgVehLinkLost:
      /* Reserved value in the ICD enum; this firmware folds "link lost" into
       * LIMITED/DECEL via the `link` checks above rather than a dedicated
       * state, so this case should not be reachable. Land in FAULT if it
       * ever is (DEV-027). */
      PRINTF("[safety] unexpected state kDgVehLinkLost reached\r\n");
      s_state = kDgVehFault;
      break;

    case kDgVehFault:
      if (!AnyFaultActive() && inputs->operator_rearm_pressed) {
        s_state = kDgVehDisarmed;
        s_faults = (dg_fault_flags_t){0};
        PRINTF("[safety] fault cleared, back to DISARMED\r\n");
      }
      break;

    case kDgVehEstop:
      if (!inputs->estop_sense_asserted && !s_faults.estop_active &&
          inputs->operator_rearm_pressed) {
        s_state = kDgVehDisarmed;
        PRINTF("[safety] e-stop released + re-armed -> DISARMED\r\n");
      }
      break;

    default:
      PRINTF("[safety] invalid state %d -- forcing FAULT (DEV-027)\r\n",
             (int)s_state);
      s_state = kDgVehFault;
      break;
  }

  (void)AlertLevelAtOrAbove; /* reserved for future per-level distraction
                                overrides; not yet needed by this FSM. */
}

/* Called by control_task once motion.c's safe-stop sequence has completed
 * (see motion.h Motion_SafeStopComplete()). Kept as a distinct entry point
 * rather than having motion.c write s_state directly, so this file remains
 * the ONLY writer of vehicle state (DEV-027 rationale). */
void Safety_NotifySafeStopComplete(void) {
  if (s_state == kDgVehDecel) {
    s_state = kDgVehStopped;
    PRINTF("[safety] -> STOPPED\r\n");
  }
}

dg_vehicle_state_t Safety_GetState(void) { return s_state; }
dg_fault_flags_t Safety_GetFaults(void) { return s_faults; }
