/*
 * DrowsyGuard VCS — motor drive: speed governing (VEH-020..023) and the
 * safe-stop sequence (VEH-040..044).
 *
 * Owns PWM1 submodules 0/1/3 (2x BTS7960 RPWM/LPWM, see board_port/pin_mux.c)
 * and the shared R_EN/L_EN enable GPIO. Duty is written from exactly one
 * place — Motion_Tick(), called once per 10 ms control tick from
 * control_task (VEH-062: "one writer for the actuators").
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef DG_MOTION_H_
#define DG_MOTION_H_

#include <stdbool.h>
#include <stdint.h>

#include "can_link.h"
#include "icd.h"

#ifdef __cplusplus
extern "C" {
#endif

void Motion_Init(void);

/* VEH-020 speed cap for the given state/link -- exposed (2026-08-16) so
 * control_task can clamp its persistent throttle setpoint down whenever the
 * cap drops (alert-driven LIMITED, or a lost link), not just the actuator
 * output. Without this, the setpoint stays at its pre-alert value and
 * silently ramps the motors back up on its own the moment the cap loosens
 * again (alert clears) -- see main.c ControlTask's "sticky speed cap"
 * comment for the full rationale. Read-only; does not itself change any
 * state. */
uint8_t Motion_GetSpeedCap(dg_vehicle_state_t state, dg_link_state_t link);

/* Driver's commanded setpoint before any cap/ramp is applied, [-100, 100]
 * per channel (sign = direction). In this bring-up firmware there is no
 * physical throttle input yet, so control_task drives this from a fixed
 * test setpoint — see main.c TODO and specs/05 OI list. */
typedef struct {
  int8_t left_pct;
  int8_t right_pct;
} dg_throttle_setpoint_t;

/* Advances ramps and safe-stop timing by dt_ms and writes PWM+direction
 * outputs. `state` selects the speed cap (VEH-020) and whether a safe stop
 * should be running (kDgVehDecel) or the driver stage should be fully
 * disabled (every state other than RUN/LIMITED/DECEL). */
void Motion_Tick(uint32_t dt_ms, dg_vehicle_state_t state, dg_link_state_t link,
                  const dg_throttle_setpoint_t *setpoint);

/* True once the safe-stop sequence (ramp -> brake -> disable) has completed
 * phase 3. safety.c polls this to drive DECEL -> STOPPED
 * (Safety_NotifySafeStopComplete(), VEH-043). Stays true until the next time
 * a safe stop is armed. */
bool Motion_SafeStopComplete(void);

/* For VCS_STATUS telemetry (spec 04 §5) — read-only snapshot of what
 * Motion_Tick() last wrote. */
typedef struct {
  uint8_t speed_cap_pct;
  uint8_t duty_left_pct;
  bool dir_left_reverse;
  uint8_t duty_right_pct;
  bool dir_right_reverse;
} dg_motion_status_t;

dg_motion_status_t Motion_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* DG_MOTION_H_ */
