/*
 * DrowsyGuard CAN ICD — wire encode/decode. See icd.h and
 * specs/04-interface-control-document.md.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "icd.h"

#include "crc8.h"

bool DG_DecodeDmsStatus(const uint8_t *payload, uint8_t dlc, dg_dms_status_t *out) {
  if (dlc != 8U) {
    return false;
  }
  if (DG_Crc8(payload, 7U) != payload[7]) {
    return false;
  }

  out->alert_level = (dg_alert_level_t)(payload[0] & 0x0FU);
  out->seq         = (uint8_t)((payload[0] >> 4) & 0x0FU);

  out->d1_state = (dg_domain_state_t)(payload[1] & 0x03U);
  out->d2_state = (dg_domain_state_t)((payload[1] >> 2) & 0x03U);
  out->d3_state = (dg_domain_state_t)((payload[1] >> 4) & 0x03U);
  out->d3_avail = (dg_d3_avail_t)((payload[1] >> 6) & 0x03U);

  out->perclos_pct     = payload[2];
  out->eye_closure_ms  = (uint16_t)(payload[3] | ((uint16_t)payload[4] << 8));
  out->face_conf_pct   = payload[5];

  out->flag_ack_refractory = (payload[6] & 0x01U) != 0U;
  out->flag_sensor_lost    = (payload[6] & 0x02U) != 0U;
  out->flag_model_degraded = (payload[6] & 0x04U) != 0U;
  out->flag_night_mode     = (payload[6] & 0x08U) != 0U;
  out->flag_calib_done     = (payload[6] & 0x10U) != 0U;
  out->flag_pipeline_slow  = (payload[6] & 0x20U) != 0U;
  out->flag_ack_saturated  = (payload[6] & 0x40U) != 0U;

  return true;
}

bool DG_DecodeDmsMetrics(const uint8_t *payload, uint8_t dlc, dg_dms_metrics_t *out) {
  if (dlc != 8U) {
    return false;
  }
  if (DG_Crc8(payload, 7U) != payload[7]) {
    return false;
  }

  out->fps_x10      = payload[0];
  out->inference_ms = payload[1];
  out->yawn_count   = payload[2];
  out->eor_cum_ms   = (uint16_t)(payload[3] | ((uint16_t)payload[4] << 8));
  out->dropped_pct  = payload[5];
  out->seq          = payload[6];

  return true;
}

uint8_t DG_EncodeVcsStatus(const dg_vcs_status_t *in, uint8_t *payload) {
  payload[0] = (uint8_t)(((uint8_t)in->vehicle_state & 0x0FU) |
                          (uint8_t)((in->seq & 0x0FU) << 4));
  payload[1] = in->speed_cap_pct;
  payload[2] = (uint8_t)((in->duty_left_pct & 0x7FU) |
                          (in->dir_left_reverse ? 0x80U : 0x00U));
  payload[3] = (uint8_t)((in->duty_right_pct & 0x7FU) |
                          (in->dir_right_reverse ? 0x80U : 0x00U));

  payload[4] = (uint8_t)((in->fault_driver ? 0x01U : 0U) |
                          (in->fault_watchdog_reset ? 0x02U : 0U) |
                          (in->fault_can_timeout ? 0x04U : 0U) |
                          (in->fault_undervoltage ? 0x08U : 0U) |
                          (in->estop_active ? 0x10U : 0U) |
                          (in->indicator_active ? 0x20U : 0U) |
                          (uint8_t)((in->indicator_dir & 0x03U) << 6));

  payload[5] = (uint8_t)(in->uptime_s & 0x00FFU);
  payload[6] = (uint8_t)((in->uptime_s >> 8) & 0x00FFU);
  payload[7] = DG_Crc8(payload, 7U);

  return 8U;
}

uint8_t DG_EncodeVcsEvent(dg_event_id_t event_id, uint8_t event_seq, uint8_t *payload) {
  payload[0] = (uint8_t)event_id;
  payload[1] = event_seq;
  return 2U;
}

uint8_t DG_EncodeEmergencyStop(dg_estop_reason_t reason, uint8_t *payload) {
  payload[0] = (uint8_t)reason;
  payload[1] = DG_ESTOP_MAGIC;
  return 2U;
}

bool DG_DecodeEmergencyStop(const uint8_t *payload, uint8_t dlc, dg_estop_reason_t *reason) {
  if (dlc != 2U) {
    return false;
  }
  if (payload[1] != DG_ESTOP_MAGIC) {
    return false; /* CAN-051 */
  }
  *reason = (dg_estop_reason_t)payload[0];
  return true;
}
