/*
 * DrowsyGuard VCS — alert actuation: buzzer, status LED, vibration, fan
 * relay, hazard lamps (specs/05-vehicle-control-spec.md §5, VEH-030..035).
 *
 * Non-blocking pattern engine driven by the control tick (VEH-034: no
 * delay(), no busy-wait anywhere in this file).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef DG_ALERTS_H_
#define DG_ALERTS_H_

#include <stdbool.h>
#include <stdint.h>

#include "icd.h"
#include "safety.h"

#ifdef __cplusplus
extern "C" {
#endif

void Alerts_Init(void);

/* Priority order (highest wins) matches specs/05 VEH-030 and specs/03 §7:
 * FAULT/ESTOP > LINK_LOST > SENSOR_LOST > alert level L3..L0.
 * `sensorLost` is DMS_STATUS.flag_sensor_lost — passed separately from
 * `level` because a sensor fault must never be rendered as a drowsiness
 * alarm (VEH-032). */
void Alerts_Tick(uint32_t dt_ms, dg_vehicle_state_t vehState, dg_alert_level_t level,
                  bool sensorLost, dg_link_state_t link, dg_fault_flags_t faults);

#ifdef __cplusplus
}
#endif

#endif /* DG_ALERTS_H_ */
