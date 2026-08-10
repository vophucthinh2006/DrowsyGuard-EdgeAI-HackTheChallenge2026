# 04 — Interface Control Document: DMS ↔ VCS CAN Bus

**Document:** DG-SPEC-04 · Rev 0.1 · 2026-08-10 · DRAFT
**Generated from:** `shared/icd/icd.yaml` — **this document and that file SHALL agree; CI enforces it**

---

## 1. Physical layer

| Property | Value | Note |
|---|---|---|
| Protocol | Classical CAN 2.0A | 11-bit identifiers only |
| Bit rate | **500 kbit/s** | Nominal bit time 2 µs |
| Sample point | **87.5 %** | CiA 301 recommendation |
| Synchronisation jump width | 1 TQ | |
| Topology | Linear, 2 nodes | No stubs > 0.3 m |
| Termination | **120 Ω at each physical end** | Total bus resistance ≈ 60 Ω, measured with power off |
| Bus length | ≤ 2 m (bench) | Well inside the 40 m limit for 500 kbit/s |
| Logic level | 3.3 V both nodes | No 5 V transceiver supply on the logic pins |
| Transceiver | 3.3 V CAN transceiver (e.g. SN65HVD230 / TJA1051T/3) one per node | |

**CAN-001** — Bus resistance SHALL be verified at 60 Ω ± 5 Ω with all power removed before any
node is enabled. A bus with one termination (120 Ω) or three (40 Ω) will appear to work at short
range and fail intermittently — this measurement is mandatory, not optional.

**CAN-002** — Both nodes SHALL share a common ground reference with the motor supply ground, and
the CAN pair SHALL be twisted for its full length.

### 1.1 Node hardware mapping

| Node | Controller | Peripheral | Status |
|---|---|---|---|
| VCS | FRDM-MCXN947 | FlexCAN (CAN0), classical mode | 🟡 DESIGNED |
| DMS | Arduino UNO Q → STM32U585 | FDCAN1 in classical CAN mode | ⚠️ **ASSUMPTION** — see §9 |

**CAN-003** — Classical CAN is used, not CAN FD, even though both controllers support FD.
*Rationale: the payload is 8 bytes; FD buys nothing here and costs bit-timing configuration that
must match exactly on two different silicon families. Every avoidable degree of freedom on a
two-day integration is a defect avoided.*

---

## 2. Message catalogue

Lower identifier = higher priority under CAN arbitration. The identifier allocation below is
deliberate: the emergency message wins arbitration against everything else on the bus.

| ID | Name | Direction | DLC | Cycle | Priority rationale |
|---|---|---|---|---|---|
| `0x080` | `EMERGENCY_STOP` | either → either | 2 | event, ≤3 repeats @10 ms | Must never lose arbitration |
| `0x100` | `DMS_STATUS` | DMS → VCS | 8 | **100 ms** | The safety-relevant periodic |
| `0x101` | `DMS_METRICS` | DMS → VCS | 8 | 500 ms | Telemetry only |
| `0x200` | `VCS_STATUS` | VCS → DMS | 8 | **100 ms** | Vehicle state feedback |
| `0x201` | `VCS_EVENT` | VCS → DMS | 2 | event | Ack button, re-arm, e-stop release |
| `0x700` | `DIAG_REQ` | DMS → VCS | 8 | on request | Lowest priority |
| `0x701` | `DIAG_RESP` | VCS → DMS | 8 | on request | Lowest priority |

**CAN-004** — No node SHALL transmit any identifier not in this table. Receivers SHALL configure
acceptance filters to this set and SHALL count unexpected identifiers as a fault statistic.

### 2.1 Bus load

| Message | Bytes | Worst-case frame bits (std, 8 data, stuffing) | Frames/s | bit/s |
|---|---:|---:|---:|---:|
| `DMS_STATUS` | 8 | ~130 | 10 | 1300 |
| `DMS_METRICS` | 8 | ~130 | 2 | 260 |
| `VCS_STATUS` | 8 | ~130 | 10 | 1300 |
| Others | — | — | negligible | ~100 |
| **Total** | | | | **≈ 3.0 kbit/s** |

Bus load ≈ **0.6 %** of 500 kbit/s. Requirement [SYS-PR-005](01-system-requirements.md#5-performance-requirements)
(≤ 5 %) is satisfied with two orders of magnitude of headroom, which is the point: this bus must
never be the reason a safety message is late.

---

## 3. `0x100 DMS_STATUS` — the safety-relevant message

**Direction:** DMS → VCS · **DLC:** 8 · **Cycle:** 100 ms ± 10 ms

| Byte | Bits | Field | Type | Encoding |
|---|---|---|---|---|
| 0 | 3:0 | `alert_level` | u4 | 0 = L0, 1 = L1, 2 = L2, 3 = L3, 4–15 reserved |
| 0 | 7:4 | `seq` | u4 | Increments modulo 16 every transmission |
| 1 | 1:0 | `d1_state` | u2 | 0 IDLE, 1 ACTIVE, 2 SEVERE, 3 reserved |
| 1 | 3:2 | `d2_state` | u2 | 0 IDLE, 1 ACTIVE, 2 SEVERE, 3 reserved |
| 1 | 5:4 | `d3_state` | u2 | 0 IDLE, 1 ACTIVE, 2 SEVERE, 3 **CRITICAL** |
| 1 | 7:6 | `d3_avail` | u2 | 0 available, 1 degraded, 2 unavailable, 3 reserved |
| 2 | 7:0 | `perclos_pct` | u8 | 0–100 = percent; **255 = INVALID** |
| 3–4 | 15:0 | `eye_closure_ms` | u16 LE | 0–65534 ms; 65535 = not measurable |
| 5 | 7:0 | `face_conf_pct` | u8 | 0–100; 255 = no face |
| 6 | 0 | `flag_ack_refractory` | bool | Ack refractory currently active |
| 6 | 1 | `flag_sensor_lost` | bool | `SENSOR_LOST` fault |
| 6 | 2 | `flag_model_degraded` | bool | `MODEL_DEGRADED` fault |
| 6 | 3 | `flag_night_mode` | bool | IR illumination active |
| 6 | 4 | `flag_calib_done` | bool | Startup calibration complete; state before this is not trustworthy |
| 6 | 5 | `flag_pipeline_slow` | bool | Achieved FPS below floor |
| 6 | 6 | `flag_ack_saturated` | bool | `ACK_MAX_CONSECUTIVE` exceeded |
| 6 | 7 | reserved | — | SHALL be 0 |
| 7 | 7:0 | `crc8` | u8 | CRC-8 SAE-J1850 (poly 0x1D, init 0xFF, xorout 0xFF) over bytes 0–6 |

**CAN-010** — Multi-byte fields are **little-endian** throughout this ICD. Stated once, applied
everywhere, no exceptions.

**CAN-011** — The receiver SHALL validate, in this order: DLC == 8 → CRC matches → `seq` equals
(previous `seq` + 1) mod 16. A frame failing any check SHALL be **discarded** and SHALL NOT refresh
the timeout supervisor.
*Rationale: a corrupted frame that still refreshes the watchdog is worse than a lost frame — it
masks a failing link with stale data.*

**CAN-012** — A `seq` discontinuity SHALL be counted in `frames_lost` diagnostics but SHALL NOT by
itself trigger the failsafe. Only the elapsed-time supervisor does that (§6).

**CAN-013** — `alert_level` is the **only** field the VCS safety path acts on. All other fields are
for indication, logging and diagnostics.
*Rationale: one field, one meaning, one place to look when the vehicle does something unexpected.*

**CAN-014** — Until `flag_calib_done` is set, the VCS SHALL remain disarmed regardless of
`alert_level` ([SYS-ER-005](01-system-requirements.md#8-environmental-and-operational-requirements)).

---

## 4. `0x101 DMS_METRICS` — telemetry

**Direction:** DMS → VCS · **DLC:** 8 · **Cycle:** 500 ms

| Byte | Field | Type | Encoding |
|---|---|---|---|
| 0 | `fps_x10` | u8 | Achieved capture-to-decision rate × 10 (e.g. 87 = 8.7 FPS) |
| 1 | `inference_ms` | u8 | Last inference time, ms, saturating at 255 |
| 2 | `yawn_count` | u8 | Yawn events in the rolling `D2_WINDOW_MS` |
| 3–4 | `eor_cum_ms` | u16 LE | Cumulative eyes-off-road in the rolling `D1_CUM_WINDOW_MS` |
| 5 | `dropped_pct` | u8 | Frames dropped in the last second, percent |
| 6 | `seq` | u8 | Free-running counter |
| 7 | `crc8` | u8 | Same algorithm as §3 |

**CAN-020** — `DMS_METRICS` SHALL NOT influence any VCS actuation decision. It exists so the bench
can see inside the pipeline without attaching a debugger.

---

## 5. `0x200 VCS_STATUS` — vehicle feedback

**Direction:** VCS → DMS · **DLC:** 8 · **Cycle:** 100 ms ± 10 ms

| Byte | Bits | Field | Type | Encoding |
|---|---|---|---|---|
| 0 | 3:0 | `vehicle_state` | u4 | 0 INIT, 1 DISARMED, 2 ARMED_IDLE, 3 RUN, 4 LIMITED, 5 DECEL, 6 STOPPED, 7 LINK_LOST, 8 FAULT, 9 ESTOP |
| 0 | 7:4 | `seq` | u4 | modulo 16 |
| 1 | 7:0 | `speed_cap_pct` | u8 | Currently applied cap, 0–100 |
| 2 | 6:0 | `duty_left_pct` | u7 | 0–100 magnitude |
| 2 | 7 | `dir_left` | bool | 0 forward, 1 reverse |
| 3 | 6:0 | `duty_right_pct` | u7 | 0–100 magnitude |
| 3 | 7 | `dir_right` | bool | 0 forward, 1 reverse |
| 4 | 0 | `fault_driver` | bool | H-bridge fault / overcurrent |
| 4 | 1 | `fault_watchdog_reset` | bool | Set for 5 s after a watchdog-induced reset |
| 4 | 2 | `fault_can_timeout` | bool | Supervisor expired |
| 4 | 3 | `fault_undervoltage` | bool | Motor rail below limit |
| 4 | 4 | `estop_active` | bool | Physical e-stop asserted |
| 4 | 5 | `indicator_active` | bool | Simulated turn indicator on (feeds D1 suppression) |
| 4 | 7:6 | `indicator_dir` | u2 | 0 none, 1 left, 2 right |
| 5–6 | | `uptime_s` | u16 LE | Seconds since boot, saturating |
| 7 | | `crc8` | u8 | Same algorithm as §3 |

**CAN-030** — The DMS SHALL use `indicator_active` / `indicator_dir` to apply the D1 mirror-check
suppression of [DOM-D1-005](03-drowsiness-domain-spec.md#34-behaviour--normative).

**CAN-031** — `fault_watchdog_reset` SHALL be logged by the DMS as an `ERROR`. A silent watchdog
reset during a demonstration that nobody notices is a lost defect.

---

## 6. `0x201 VCS_EVENT` — driver and operator inputs

**Direction:** VCS → DMS · **DLC:** 2 · **Event-driven**

| Byte | Field | Encoding |
|---|---|---|
| 0 | `event_id` | 1 = ACK ("I am awake") pressed, 2 = operator RE-ARM, 3 = e-stop asserted, 4 = e-stop released, 5 = indicator on, 6 = indicator off |
| 1 | `event_seq` | Free-running counter, increments per event |

**CAN-040** — Event messages SHALL be transmitted **three times at 10 ms intervals** with the same
`event_seq`. The receiver SHALL de-duplicate on `event_seq`.
*Rationale: an event message has no periodic repetition to recover it. A single lost ACK frame
means a driver pressed the button and the system ignored them — which is precisely the experience
that destroys trust. Triple transmission with de-duplication costs 3 frames and removes the class
of failure.*

**CAN-041** — The ACK button SHALL be debounced on the VCS in hardware/firmware (≥ 20 ms) before an
event is emitted.

---

## 7. `0x080 EMERGENCY_STOP`

**Direction:** either → either · **DLC:** 2 · **Event, repeated 3× @ 10 ms**

| Byte | Field | Encoding |
|---|---|---|
| 0 | `reason` | 1 = physical e-stop, 2 = DMS critical fault, 3 = VCS critical fault, 4 = operator command |
| 1 | `magic` | SHALL be `0x5A`; any other value SHALL cause the frame to be ignored |

**CAN-050** — On receipt of a valid `EMERGENCY_STOP`, the VCS SHALL immediately disable the motor
driver outputs (no ramp) and enter `ESTOP`. Recovery SHALL require an operator re-arm.

**CAN-051** — The `magic` byte exists so that a bus-error-induced spurious short frame at the
highest-priority identifier cannot stop the vehicle by accident. It is a cheap guard on the one
message with the most authority.

**CAN-052** — `EMERGENCY_STOP` over CAN is a **convenience path, not the safety path**. The
authoritative emergency stop is the physical de-energising switch required by
[SYS-SR-005](01-system-requirements.md#7-safety-requirements), which works with the firmware
hung.

---

## 8. Timeout supervision — **NORMATIVE**

This section defines the single most important failure behaviour in the system.

| Parameter | Symbol | Value |
|---|---|---|
| `DMS_STATUS` nominal cycle | `T_CYCLE` | 100 ms |
| Degrade threshold (3 missed cycles) | `CAN_DEGRADE_MS` | **300 ms** |
| Safe-stop threshold | `CAN_TIMEOUT_MS` | **1000 ms** |
| Recovery: valid frames required to leave `LINK_LOST` | `CAN_RECOVER_FRAMES` | 5 consecutive |

**CAN-060** — The VCS SHALL maintain a supervisor timer reset **only** by a `DMS_STATUS` frame that
passes every check in CAN-011.

**CAN-061** — At `CAN_DEGRADE_MS` with no valid frame, the VCS SHALL enter `LINK_LOST`, apply the
degraded speed cap (30 %, see [05 §7](05-vehicle-control-spec.md#7-failsafe-behaviour)) and start
the amber fault indication.

**CAN-062** — At `CAN_TIMEOUT_MS` with no valid frame, the VCS SHALL execute a full safe stop.

**CAN-063** — Absence of `DMS_STATUS` SHALL NEVER be interpreted as `alert_level = L0`. The last
received level SHALL NOT be held indefinitely.
*Rationale: this is the classic latent bug in every two-node design — the receiver keeps using the
last value it got, and a dead link looks exactly like a perfectly alert driver. It is silent, it
passes every functional test, and it only appears when the cable falls out during the demo.*

**CAN-064** — Recovery from `LINK_LOST` SHALL require `CAN_RECOVER_FRAMES` consecutive valid
frames, and SHALL re-enter at the level indicated by those frames — not at the pre-fault level.

**CAN-065** — The DMS SHALL apply the mirror-image supervision to `VCS_STATUS`: at 300 ms with no
valid frame, log `ERROR`, indicate a link fault locally, and continue publishing status (the DMS
has nothing to fall back to; its job is to keep telling the truth on the bus).

**CAN-066** — Bus-off recovery SHALL be automatic, and each bus-off occurrence SHALL be counted and
reported in `DIAG_RESP`. A demonstration that experienced a bus-off is not a clean run.

---

## 9. Open items

| ID | Item | Impact | Owner | Due |
|---|---|---|---|---|
| **OI-04-01** ⚠️ | The STM32U585 on the Arduino UNO Q is assumed to expose FDCAN1 TX/RX on header-reachable pins. **Unconfirmed against the UNO Q schematic/pinout.** | If false, the whole DMS→VCS link must be re-planned | Hardware lead | +24 h from board arrival |
| **OI-04-02** | Fallback if OI-04-01 fails: SPI CAN controller (MCP2515-class) on the DMS side. Adds ≈1 ms latency and one day of bring-up. Second fallback: UART + framed protocol with the same payloads. | Schedule | Hardware lead | Decision within 24 h |
| **OI-04-03** | Bit-timing register values for FlexCAN (MCXN947) and FDCAN (STM32U585) at 500 kbit/s / 87.5 % SP must be computed from each part's actual peripheral clock and cross-verified with a scope | Intermittent errors if mismatched | Firmware leads | Before first two-node bring-up |
| **OI-04-04** | CRC-8 implementation must be bit-identical on both sides — verify with a shared test vector before integration, not during it | Total link failure that looks like wiring | ICD owner | Before integration |
| **OI-04-05** | Transceiver part selection and whether the UNO Q can supply the transceiver's 3.3 V rail at the required current | Bring-up blocker | Hardware lead | +24 h |

**CAN-070** — OI-04-04's test vector SHALL be committed as `shared/icd/crc_vectors.csv` and SHALL
be executed as a unit test on **both** node builds. Two independently written CRCs that disagree
produce a link where every frame is discarded and every symptom points at the wiring.

---

## 10. Bring-up checklist

Do these in order. Do not skip ahead when it "should work".

1. [ ] Power off. Measure bus resistance = 60 Ω ± 5 Ω (CAN-001).
2. [ ] Scope CAN_H / CAN_L: recessive ≈ 2.5 V both, dominant ≈ 3.5 V / 1.5 V.
3. [ ] Single node transmitting into a terminated bus with **no** second node: confirm the frame is
       repeated indefinitely (no ACK) — this proves the transmitter and the timing without needing
       both sides working.
4. [ ] Measure actual bit time on the scope = 2.00 µs ± 1 %. Both nodes independently.
5. [ ] Both nodes on the bus: confirm ACK, error counters stay at 0 for 60 s.
6. [ ] CRC test vector passes on both builds (CAN-070).
7. [ ] Run `tools/can_inject.py` to drive the VCS through every `alert_level` with no DMS attached.
8. [ ] Unplug the bus mid-run: verify `LINK_LOST` at 300 ms and safe stop at 1000 ms with a scope
       on the actuator line (TC-SAF-004, TC-SAF-005).

---

## Revision history

| Rev | Date | Author | Change |
|---|---|---|---|
| 0.1 | 2026-08-10 | ML_IoT_Love50 | Initial baseline |
