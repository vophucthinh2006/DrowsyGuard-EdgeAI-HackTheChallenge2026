/*
 * DrowsyGuard CAN ICD — wire encode/decode for the DMS-simulator test node.
 * See icd.h for why this is a separate (not shared) copy from vcs-mcxn947.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "icd.h"

#include "crc8.h"

uint8_t DG_EncodeDmsStatus(const dg_dms_status_t *in, uint8_t *payload) {
  payload[0] = (uint8_t)(((uint8_t)in->alert_level & 0x0FU) |
                          (uint8_t)((in->seq & 0x0FU) << 4));

  payload[1] = (uint8_t)(((uint8_t)in->d1_state & 0x03U) |
                          (uint8_t)(((uint8_t)in->d2_state & 0x03U) << 2) |
                          (uint8_t)(((uint8_t)in->d3_state & 0x03U) << 4) |
                          (uint8_t)(((uint8_t)in->d3_avail & 0x03U) << 6));

  payload[2] = in->perclos_pct;
  payload[3] = (uint8_t)(in->eye_closure_ms & 0x00FFU);
  payload[4] = (uint8_t)((in->eye_closure_ms >> 8) & 0x00FFU);
  payload[5] = in->face_conf_pct;

  payload[6] = (uint8_t)((in->flag_ack_refractory ? 0x01U : 0U) |
                          (in->flag_sensor_lost ? 0x02U : 0U) |
                          (in->flag_model_degraded ? 0x04U : 0U) |
                          (in->flag_night_mode ? 0x08U : 0U) |
                          (in->flag_calib_done ? 0x10U : 0U) |
                          (in->flag_pipeline_slow ? 0x20U : 0U) |
                          (in->flag_ack_saturated ? 0x40U : 0U));

  payload[7] = DG_Crc8(payload, 7U);
  return 8U;
}

uint8_t DG_EncodeDmsMetrics(const dg_dms_metrics_t *in, uint8_t *payload) {
  payload[0] = in->fps_x10;
  payload[1] = in->inference_ms;
  payload[2] = in->yawn_count;
  payload[3] = (uint8_t)(in->eor_cum_ms & 0x00FFU);
  payload[4] = (uint8_t)((in->eor_cum_ms >> 8) & 0x00FFU);
  payload[5] = in->dropped_pct;
  payload[6] = in->seq;
  payload[7] = DG_Crc8(payload, 7U);
  return 8U;
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

bool DG_DecodeVcsStatus(const uint8_t *payload, uint8_t dlc, dg_vcs_status_t *out) {
  if (dlc != 8U) {
    return false;
  }
  if (DG_Crc8(payload, 7U) != payload[7]) {
    return false;
  }

  out->vehicle_state = (dg_vehicle_state_t)(payload[0] & 0x0FU);
  out->seq           = (uint8_t)((payload[0] >> 4) & 0x0FU);

  out->speed_cap_pct = payload[1];

  out->duty_left_pct    = (uint8_t)(payload[2] & 0x7FU);
  out->dir_left_reverse = (payload[2] & 0x80U) != 0U;
  out->duty_right_pct    = (uint8_t)(payload[3] & 0x7FU);
  out->dir_right_reverse = (payload[3] & 0x80U) != 0U;

  out->fault_driver         = (payload[4] & 0x01U) != 0U;
  out->fault_watchdog_reset = (payload[4] & 0x02U) != 0U;
  out->fault_can_timeout    = (payload[4] & 0x04U) != 0U;
  out->fault_undervoltage   = (payload[4] & 0x08U) != 0U;
  out->estop_active         = (payload[4] & 0x10U) != 0U;
  out->indicator_active     = (payload[4] & 0x20U) != 0U;
  out->indicator_dir        = (uint8_t)((payload[4] >> 6) & 0x03U);

  out->uptime_s = (uint16_t)(payload[5] | ((uint16_t)payload[6] << 8));

  return true;
}

bool DG_DecodeVcsEvent(const uint8_t *payload, uint8_t dlc, dg_event_id_t *event_id,
                        uint8_t *event_seq) {
  if (dlc != 2U) {
    return false;
  }
  *event_id  = (dg_event_id_t)payload[0];
  *event_seq = payload[1];
  return true;
}
