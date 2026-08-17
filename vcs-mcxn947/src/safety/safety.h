/*
 * DrowsyGuard VCS — safety state machine.
 *
 * Implements the state diagram in specs/05-vehicle-control-spec.md §3
 * (VEH-010..014) and the failsafe rules in §7 (VEH-050..056). This module
 * owns dg_vehicle_state_t; nothing else is allowed to write it, so every
 * "is this transition legal" question has exactly one place it is answered
 * (DEV-027: one switch, one default that logs and enters FAULT).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef DG_SAFETY_H_
#define DG_SAFETY_H_

#include <stdbool.h>
#include <stdint.h>

#include "can_link.h"
#include "icd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Physical inputs sampled once per control tick (VEH peripheral table).
 * Simplified 2026-08-15: no operator re-arm/ACK/e-stop-sense buttons exist
 * on this board anymore (system block diagram only has gas/brake) -- every
 * recovery transition that used to need a button press now happens
 * automatically once conditions are safe again, see safety.c's
 * SafeConditionsSustained(). */
typedef struct {
  bool throttle_nonzero;       /* VEH-011: ARMED_IDLE -> RUN */
  uint16_t motor_current_ma;   /* 0 = "not wired yet", see OI-05-01 */
  uint16_t motor_rail_mv;      /* 0 = "not wired yet", see OI-05-01 */
} dg_safety_inputs_t;

typedef struct {
  bool fault_driver;
  bool fault_watchdog_reset;
  bool fault_can_timeout;
  bool fault_undervoltage;
  bool estop_active;
} dg_fault_flags_t;

void Safety_Init(void);

/* Advances the state machine by one control tick (10 ms, VEH-060). Reads the
 * latest DMS_STATUS + link state from can_link.c directly (both are
 * read-only views, CAN-013 — only alert_level and flag_calib_done are ever
 * used for a decision) so callers just pass local inputs. */
void Safety_Tick(uint32_t now_ms, const dg_safety_inputs_t *inputs);

dg_vehicle_state_t Safety_GetState(void);
dg_fault_flags_t Safety_GetFaults(void);

/* Called by control_task once motion.c reports its safe-stop sequence
 * (VEH-040..044) has reached phase 3 (motors disabled). This is the only
 * way DECEL -> STOPPED happens (VEH-043) — motion.c never writes vehicle
 * state directly. */
void Safety_NotifySafeStopComplete(void);

/* True for exactly 5 s after a watchdog-induced reset is detected at boot
 * (VEH-053) — motion.c and alerts.c don't need this, it is surfaced only for
 * the VCS_STATUS fault_watchdog_reset bit. */
bool Safety_WatchdogResetFlagActive(uint32_t now_ms);

/* Called once from main() before the scheduler starts: reads the reset
 * cause register and, if it was WWDT0, arms Safety_WatchdogResetFlagActive()
 * and forces the post-boot state to DISARMED rather than resuming silently
 * (VEH-053). */
void Safety_CheckResetCause(uint32_t now_ms);

/* Services the hardware watchdog. Call ONLY from the end of a control tick
 * that actually completed (VEH-052) — never from an ISR, never
 * unconditionally. */
void Safety_ServiceWatchdog(void);

#ifdef __cplusplus
}
#endif

#endif /* DG_SAFETY_H_ */
