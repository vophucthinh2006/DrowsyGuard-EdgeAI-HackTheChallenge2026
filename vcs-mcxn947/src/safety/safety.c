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

static bool s_canEstopLatched = false;

/* Auto-recovery replaces the old operator-re-arm-button gate (2026-08-15,
 * system block diagram simplification -- no re-arm/ACK/e-stop-sense
 * buttons exist on this board anymore, only gas/brake). AUTO_REARM_HOLD_MS
 * requires the safe condition to hold continuously for this long before a
 * STOPPED/FAULT/ESTOP recovery transition fires, so a single good frame
 * after a bad stretch can't immediately bounce the vehicle back into
 * motion -- the closest equivalent left to the debounce a physical button
 * press used to provide. This is a deliberate trade against VEH-012's
 * original "level alone never leaves STOPPED" principle (product decision,
 * not a firmware bug) -- see README. */
#define AUTO_REARM_HOLD_MS (1000U)
static uint32_t s_safeSinceMs; /* 0 = "not currently in a sustained-safe window" */


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

  dg_estop_reason_t reason;
  if (CanLink_EmergencyStopReceived(&reason)) {
    s_canEstopLatched = true;
    CanLink_ClearEmergencyStopReceived();
  }
  /* Only source of estop_active left after removing the physical e-stop
   * sense loop (2026-08-15): a CAN-received EMERGENCY_STOP. Physical e-stop
   * is now purely electrical (cuts actuator power directly, see the system
   * block diagram's "Physical E-Stop" note inside Vehicle Actuators) --
   * this firmware never senses it, by design. */
  s_faults.estop_active = s_canEstopLatched;

  /* fault_can_timeout only means something once a DMS-AP peer has actually
   * been seen at least once (2026-08-15: standalone gas/brake-only bench
   * testing, no CAN peer ever attached, must not be treated as a timeout
   * fault -- see Safety_Tick()'s level-fallback comment for the matching
   * logic on the vehicle-state side). */
  dg_dms_status_t dmsUnused;
  bool everHadDms = CanLink_GetLatestDmsStatus(&dmsUnused);
  dg_link_state_t link = CanLink_GetLinkState();
  s_faults.fault_can_timeout = everHadDms && (link == kDgLinkLost);
}

static bool AnyFaultActive(void) {
  return s_faults.fault_driver || s_faults.fault_can_timeout ||
         s_faults.fault_undervoltage;
  /* fault_watchdog_reset and estop_active are surfaced but handled by their
   * own explicit transitions below, not folded into the generic FAULT path
   * — an e-stop goes to ESTOP (recoverable once conditions are safe again),
   * a stale watchdog flag alone does not re-trigger FAULT after the 5 s
   * window. */
}

/* True once a driver-known, CAN-link-OK, L0/Normal condition has held
 * continuously for AUTO_REARM_HOLD_MS -- see that macro's comment for why.
 * Drives every recovery transition (STOPPED/FAULT/ESTOP -> onward) now that
 * no operator re-arm button exists on this board. */
static bool SafeConditionsSustained(uint32_t now_ms, bool driverStateKnown,
                                     dg_alert_level_t level, dg_link_state_t link) {
  bool safeNow = driverStateKnown && (level == kDgAlertL0Normal) && (link == kDgLinkOk);
  if (!safeNow) {
    s_safeSinceMs = 0U;
    return false;
  }
  if (s_safeSinceMs == 0U) {
    s_safeSinceMs = now_ms;
  }
  return (now_ms - s_safeSinceMs) >= AUTO_REARM_HOLD_MS;
}

/* ---- state machine (VEH-010..014, DEV-027: one switch, default -> FAULT) - */

void Safety_Tick(uint32_t now_ms, const dg_safety_inputs_t *inputs) {
  EvaluateFaults(now_ms, inputs);

  dg_dms_status_t dms;
  bool haveDms      = CanLink_GetLatestDmsStatus(&dms);
  dg_link_state_t link = CanLink_GetLinkState();

  /* CAN-063: absence of a valid frame is NEVER treated as "driver is fine"
   * -- but only once a DMS-AP peer has been seen on the bus at least once.
   * If no DMS-AP has EVER connected (haveDms stays false forever -- e.g.
   * standalone gas/brake-only motor bench testing, 2026-08-15, no CAN peer
   * attached at all), that's "no monitor attached yet", not "danger": SW2/
   * SW3 alone can drive the motors without ever needing a CAN peer first.
   * Once a DMS-AP HAS been seen and then goes silent, CAN-063's original
   * strict fail-safe applies in full below (level forced to L3, a lost
   * link forces DECEL) -- losing an established monitor is genuinely
   * dangerous, "never had one" is not. */
  bool driverStateKnown = haveDms && (link != kDgLinkLost);
  dg_alert_level_t level = driverStateKnown  ? dms.alert_level
                            : haveDms        ? kDgAlertL3Danger
                                              : kDgAlertL0Normal;
  bool linkLostDangerous = haveDms && (link == kDgLinkLost);
  bool autoRearm          = SafeConditionsSustained(now_ms, driverStateKnown, level, link);

  /* E-stop pre-empts everything except INIT (still self-testing). Only
   * source left is a CAN-received EMERGENCY_STOP (s_faults.estop_active) --
   * see EvaluateFaults()'s comment on why the physical e-stop sense loop is
   * gone. */
  bool justEnteredEstop = false;
  if (s_state != kDgVehInit && s_faults.estop_active) {
    if (s_state != kDgVehEstop) {
      PRINTF("[safety] -> ESTOP\r\n");
      /* Bug found 2026-08-16: `autoRearm` above was computed from
       * s_safeSinceMs as it stood BEFORE this e-stop -- if level/link had
       * already been safe for >= AUTO_REARM_HOLD_MS a moment ago (the
       * common case: sim_uart keeps re-injecting L0 regardless of estop
       * commands), the switch below would see a stale autoRearm==true and
       * fall straight through ESTOP -> DISARMED in this SAME tick, before
       * ESTOP was ever actually observed as the settled state. Reset the
       * timer here (the exact tick of entry) and gate the switch's
       * recovery check on `justEnteredEstop` below so recovery always needs
       * a fresh AUTO_REARM_HOLD_MS measured from this moment forward. */
      s_safeSinceMs = 0U;
      justEnteredEstop = true;
    }
    s_state = kDgVehEstop;
  }

  /* Debug: log EVERY vehicle_state transition, not just the ones the switch
   * below already PRINTFs individually -- added 2026-08-15 because
   * RUN<->LIMITED (driven by alert_level/link from the DMS) had no log at
   * all, making it impossible to see from the console whether a signal
   * received from the UNO Q was actually changing anything. Generic
   * before/after compare catches every transition regardless of which
   * branch caused it. */
  dg_vehicle_state_t stateBeforeTick = s_state;

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
      /* No re-arm button, no calib_done gate anymore (2026-08-15
       * simplification -- was VEH-011's "button AND calib_done"). Arms
       * immediately: gas/brake alone should drive the motors with nothing
       * else to configure first. */
      s_state = kDgVehArmedIdle;
      break;

    case kDgVehArmedIdle:
      if (AnyFaultActive()) {
        s_state = kDgVehFault;
        break;
      }
      if (inputs->throttle_nonzero) {
        s_state = kDgVehRun;
      }
      break;

    case kDgVehRun:
    case kDgVehLimited:
      /* Real hardware faults (driver overcurrent, undervoltage) go straight
       * to FAULT -- continuing to drive the motor even for a controlled
       * ramp risks compounding the fault. Deliberately NOT using
       * AnyFaultActive() here: it also folds in fault_can_timeout, which is
       * literally defined as (link == kDgLinkLost) in EvaluateFaults(). If
       * that were included, this check would always win over the
       * level==L3/link==LinkLost branch below on a lost link -- making
       * CAN-062's "execute a full safe stop" (the DECEL ramp+brake
       * sequence) unreachable via its own trigger, silently replaced by an
       * abrupt FAULT-path motor disable (immediate coast, no brake). Found
       * by testing: pausing DMS_STATUS injection while in RUN landed in
       * FAULT instead of DECEL. */
      if (s_faults.fault_driver || s_faults.fault_undervoltage) {
        s_state = kDgVehFault;
        break;
      }
      if (level == kDgAlertL3Danger || linkLostDangerous) {
        s_state = kDgVehDecel; /* VEH-040, CAN-062 */
        break;
      }
      if (level == kDgAlertL1Early || level == kDgAlertL2Drowsy ||
          (haveDms && link == kDgLinkDegraded)) {
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
      /* Was VEH-012/SYS-FR-033 "level alone can never leave STOPPED",
       * enforced by requiring an operator re-arm button press. That button
       * no longer exists (2026-08-15 simplification) -- auto-recovers once
       * SafeConditionsSustained() has held for AUTO_REARM_HOLD_MS, a
       * deliberate product trade-off, not an oversight (see that macro's
       * comment). */
      if (autoRearm) {
        s_state = kDgVehArmedIdle;
        s_faults = (dg_fault_flags_t){0};
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
      if (!AnyFaultActive()) {
        s_state = kDgVehDisarmed;
        s_faults = (dg_fault_flags_t){0};
      }
      break;

    case kDgVehEstop:
      /* justEnteredEstop: never recover on the same tick ESTOP was entered
       * -- `autoRearm` above may still reflect safe time banked before this
       * e-stop even though s_safeSinceMs was just reset, since it was
       * computed earlier in this same function call. Next tick recomputes
       * autoRearm from the now-reset timer, so recovery still needs a full
       * fresh AUTO_REARM_HOLD_MS from here. */
      if (!justEnteredEstop && autoRearm) {
        s_canEstopLatched = false;
        s_faults.estop_active = false;
        s_state = kDgVehDisarmed;
      }
      break;

    default:
      PRINTF("[safety] invalid state %d -- forcing FAULT (DEV-027)\r\n",
             (int)s_state);
      s_state = kDgVehFault;
      break;
  }

  if (s_state != stateBeforeTick) {
    static const char *const kStateNames[] = {"INIT",   "DISARMED", "ARMED_IDLE", "RUN",
                                               "LIMITED", "DECEL",    "STOPPED",    "LINK_LOST",
                                               "FAULT",   "ESTOP"};
    PRINTF("[safety] state %s -> %s (level=%u link=%u haveDms=%u)\r\n",
           kStateNames[(unsigned int)stateBeforeTick], kStateNames[(unsigned int)s_state],
           (unsigned int)level, (unsigned int)link, (unsigned int)haveDms);

    /* FAULT is entered from inside the switch above (Disarmed/ArmedIdle/
     * Run/Limited -> Fault), unlike ESTOP which is forced by the pre-empt
     * block above the switch (and resets s_safeSinceMs right there, see
     * that comment) -- so this is the first and only place a fresh FAULT
     * entry can reset the timer before FAULT's own recovery check
     * (AnyFaultActive(), not autoRearm) is reached on a later tick. */
    if (s_state == kDgVehFault) {
      s_safeSinceMs = 0U;
    }
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
