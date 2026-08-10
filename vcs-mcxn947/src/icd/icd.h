/*
 * DrowsyGuard CAN Interface Control Document — wire format.
 *
 * Byte-exact implementation of ../../../shared/icd/icd.yaml (the canonical,
 * machine-readable message catalogue) and specs/04-interface-control-document.md.
 * This is the C side of the ICD; ../../../dms-ap/src/drowsyguard/link/icd.py
 * is the other. `shared/icd/generate.py` does not exist yet (specs/02
 * DEV-002 is still open — see ../../../shared/icd/README.md for exactly what
 * that means), so this file is hand-written and hand-kept in sync with the
 * Python side rather than generated. Any field change here SHALL also be
 * made in icd.yaml and icd.py in the same commit (DEV-092).
 *
 * All multi-byte fields are little-endian (CAN-010).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef DG_ICD_H_
#define DG_ICD_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- CAN identifiers (11-bit standard, spec 04 §2) ---------------------- */
#define DG_CANID_EMERGENCY_STOP (0x080U) /* either -> either,  DLC 2 */
#define DG_CANID_DMS_STATUS     (0x100U) /* DMS -> VCS,        DLC 8, 100 ms */
#define DG_CANID_DMS_METRICS    (0x101U) /* DMS -> VCS,        DLC 8, 500 ms */
#define DG_CANID_VCS_STATUS     (0x200U) /* VCS -> DMS,        DLC 8, 100 ms */
#define DG_CANID_VCS_EVENT      (0x201U) /* VCS -> DMS,        DLC 2, event */
#define DG_CANID_DIAG_REQ       (0x700U) /* DMS -> VCS,        DLC 8, on request (not yet implemented) */
#define DG_CANID_DIAG_RESP      (0x701U) /* VCS -> DMS,        DLC 8, on request (not yet implemented) */

/* ---- shared enums --------------------------------------------------------*/

/* DMS_STATUS byte0 bits3:0, VCS speed-cap driver (05 §4). */
typedef enum {
  kDgAlertL0Normal = 0,
  kDgAlertL1Early  = 1,
  kDgAlertL2Drowsy = 2,
  kDgAlertL3Danger = 3,
} dg_alert_level_t;

/* d1_state / d2_state (2-bit domain ladder, D3 additionally uses value 3). */
typedef enum {
  kDgDomainIdle   = 0,
  kDgDomainActive = 1,
  kDgDomainSevere = 2,
  kDgDomainCritical = 3, /* D3 only */
} dg_domain_state_t;

typedef enum {
  kDgD3Available = 0,
  kDgD3Degraded   = 1,
  kDgD3Unavailable = 2,
} dg_d3_avail_t;

/* VCS_STATUS byte0 bits3:0 — specs/05-vehicle-control-spec.md §3. */
typedef enum {
  kDgVehInit      = 0,
  kDgVehDisarmed  = 1,
  kDgVehArmedIdle = 2,
  kDgVehRun       = 3,
  kDgVehLimited   = 4,
  kDgVehDecel     = 5,
  kDgVehStopped   = 6,
  kDgVehLinkLost  = 7,
  kDgVehFault     = 8,
  kDgVehEstop     = 9,
} dg_vehicle_state_t;

typedef enum {
  kDgEventAck          = 1, /* "I am awake" pressed */
  kDgEventOperatorRearm = 2,
  kDgEventEstopAsserted = 3,
  kDgEventEstopReleased = 4,
  kDgEventIndicatorOn   = 5,
  kDgEventIndicatorOff  = 6,
} dg_event_id_t;

typedef enum {
  kDgEstopReasonPhysical  = 1,
  kDgEstopReasonDmsFault  = 2,
  kDgEstopReasonVcsFault  = 3,
  kDgEstopReasonOperator  = 4,
} dg_estop_reason_t;

#define DG_ESTOP_MAGIC (0x5AU) /* CAN-051 */

/* ---- 0x100 DMS_STATUS — decoded form (spec 04 §3) ------------------------ */
typedef struct {
  dg_alert_level_t alert_level;
  uint8_t seq; /* 4-bit, 0..15 */

  dg_domain_state_t d1_state;
  dg_domain_state_t d2_state;
  dg_domain_state_t d3_state;
  dg_d3_avail_t d3_avail;

  uint8_t perclos_pct; /* 0..100, 255 = invalid */
  uint16_t eye_closure_ms; /* 65535 = not measurable */
  uint8_t face_conf_pct; /* 255 = no face */

  bool flag_ack_refractory;
  bool flag_sensor_lost;
  bool flag_model_degraded;
  bool flag_night_mode;
  bool flag_calib_done;
  bool flag_pipeline_slow;
  bool flag_ack_saturated;
} dg_dms_status_t;

/* ---- 0x101 DMS_METRICS — decoded form (spec 04 §4), telemetry only ------- */
typedef struct {
  uint8_t fps_x10;
  uint8_t inference_ms;
  uint8_t yawn_count;
  uint16_t eor_cum_ms;
  uint8_t dropped_pct;
  uint8_t seq;
} dg_dms_metrics_t;

/* ---- 0x200 VCS_STATUS — encoder input (spec 04 §5) ----------------------- */
typedef struct {
  dg_vehicle_state_t vehicle_state;
  uint8_t seq; /* 4-bit, 0..15, caller increments */

  uint8_t speed_cap_pct;
  uint8_t duty_left_pct;  /* 0..100 magnitude */
  bool dir_left_reverse;
  uint8_t duty_right_pct;
  bool dir_right_reverse;

  bool fault_driver;
  bool fault_watchdog_reset;
  bool fault_can_timeout;
  bool fault_undervoltage;
  bool estop_active;
  bool indicator_active;
  uint8_t indicator_dir; /* 0 none, 1 left, 2 right */

  uint16_t uptime_s;
} dg_vcs_status_t;

/* ---- wire encode/decode -------------------------------------------------- */

/* Returns false (and leaves *out untouched) if dlc != 8, CRC fails, or the
 * rolling seq is not exactly (prev_seq + 1) mod 16 -- caller decides whether
 * a seq gap is a discarded frame or just a counted loss (CAN-011/CAN-012;
 * this function only performs the CRC/DLC check, seq continuity is the
 * caller's business since it needs the previous seq). */
bool DG_DecodeDmsStatus(const uint8_t *payload, uint8_t dlc, dg_dms_status_t *out);
bool DG_DecodeDmsMetrics(const uint8_t *payload, uint8_t dlc, dg_dms_metrics_t *out);

/* Encodes into an 8-byte payload (payload[7] = CRC). Returns the DLC (8). */
uint8_t DG_EncodeVcsStatus(const dg_vcs_status_t *in, uint8_t *payload);

/* 0x201 VCS_EVENT — 2 bytes: [event_id, event_seq]. */
uint8_t DG_EncodeVcsEvent(dg_event_id_t event_id, uint8_t event_seq, uint8_t *payload);

/* 0x080 EMERGENCY_STOP — 2 bytes: [reason, magic]. */
uint8_t DG_EncodeEmergencyStop(dg_estop_reason_t reason, uint8_t *payload);
/* Returns false if magic != DG_ESTOP_MAGIC or dlc != 2 (CAN-051). */
bool DG_DecodeEmergencyStop(const uint8_t *payload, uint8_t dlc, dg_estop_reason_t *reason);

#ifdef __cplusplus
}
#endif

#endif /* DG_ICD_H_ */
