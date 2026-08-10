# 07 — Test Case Catalogue

**Document:** DG-SPEC-07 · Rev 0.1 · 2026-08-10 · DRAFT
**Method and instruments:** [06 — Test Plan](06-test-plan.md)
**Results are recorded in:** [08 — Benchmark Log](08-benchmark-log.md)

---

## How to use this catalogue

Each case states its **level** (L1 unit · L2 corpus replay · L3 node integration · L4 system ·
L5 acceptance), the requirement it verifies, and a **pass criterion that is checkable without
judgement**. If two engineers could disagree about whether a case passed, the case is badly written
— fix it rather than argue about it.

Status column values: `PASS` · `FAIL` · `BLOCKED` · `N/R` (not run). Every `PASS` for a case that
produces a number SHALL cite the benchmark run ID that produced it.

**No case is complete until its result is written down.** A test that was "run and it looked fine"
did not happen.

---

## 1. TC-ARC — Architecture and privacy

| ID | Level | Verifies | Procedure | Pass criterion | Status |
|---|---|---|---|---|---|
| TC-ARC-001 | L4 | SYS-AR-002 | Disconnect the camera; drive the VCS with `can_inject.py` through L0→L3 | VCS executes all levels and the safe stop correctly with no camera in the system | N/R |
| TC-ARC-002 | L4 | SYS-AR-003 | With the vehicle at L2, `kill -9` the DMS-AP process | VCS detects link loss and reaches safe stop per AC-09/AC-10; no undefined behaviour | N/R |
| TC-ARC-003 | L4 | SYS-AR-004 | Capture 5 min of CAN traffic; parse every frame | No frame contains image data, landmark coordinates, or any field not listed in spec 04 | N/R |
| TC-ARC-004 | L3 | SYS-AR-005 | Disable all network interfaces on the DMS-AP; run a full C-DROWSY replay | Identical alert-level timeline to the networked run, byte for byte | N/R |
| TC-ARC-005 | L3 | SYS-AR-006 | Run a session, power-cycle, inspect filesystem for new/modified files | Only logs (containing no PII) changed; no driver state persisted | N/R |
| TC-ARC-006 | L1 | DEV-062 | `grep` the log output of a full replay for coordinate-like and base64-like patterns | Zero matches | N/R |

---

## 2. TC-DOM — Domain detection

### 2.1 D1 Distraction

| ID | Level | Verifies | Procedure | Pass criterion | Status |
|---|---|---|---|---|---|
| TC-DOM-001 | L1 | DOM-D1-001 | Synthetic sequence: off-road for 500 ms, then on-road | D1 stays IDLE; EOR contribution = 0 | N/R |
| TC-DOM-002 | L1 | DOM-D1-002 | Off-road continuously 2000 ms | D1 = ACTIVE at t = 2000 ms ± 1 frame period | N/R |
| TC-DOM-003 | L1 | DOM-D1-002 | Off-road continuously 1900 ms then on-road | D1 never leaves IDLE | N/R |
| TC-DOM-004 | L1 | DOM-D1-003 | Six 1.1 s off-road glances inside a 12 s window | D1 = SEVERE once cumulative reaches 6000 ms | N/R |
| TC-DOM-005 | L1 | DOM-D1-003 | Off-road continuously 4000 ms | D1 = SEVERE | N/R |
| TC-DOM-006 | L1 | DOM-D1-004 | From ACTIVE, on-road for 2900 ms then off-road again | D1 does not de-escalate | N/R |
| TC-DOM-007 | L1 | DOM-D1-005 | Indicator right asserted, head yaws right 3 s | D1 stays IDLE | N/R |
| TC-DOM-008 | L1 | DOM-D1-005 | Indicator right asserted, head **pitches down** 3 s | D1 = ACTIVE — pitch is not suppressed | N/R |
| TC-DOM-009 | L1 | DOM-D1-005 | Indicator right asserted, head yaws **left** 3 s | D1 = ACTIVE — only the indicated direction is suppressed | N/R |
| TC-DOM-010 | L2 | AC-04 | Replay C-DROWSY held-out split; match against `eyes_off_road` annotations | TPR ≥ 0.90 | N/R |

### 2.2 D2 Yawning

| ID | Level | Verifies | Procedure | Pass criterion | Status |
|---|---|---|---|---|---|
| TC-DOM-011 | L1 | DOM-D2-001 | Mouth-open episode of 1400 ms | No yawn event registered | N/R |
| TC-DOM-012 | L1 | DOM-D2-001 | Mouth-open episode of 1600 ms | Exactly one yawn event, timestamped at episode end | N/R |
| TC-DOM-013 | L2 | DOM-D2-001 | Replay 3 min of continuous speech from C-BASE | Zero yawn events registered | N/R |
| TC-DOM-014 | L2 | DOM-D2-001 | Replay laughing and eating clips from C-BASE | Zero yawn events registered | N/R |
| TC-DOM-015 | L1 | DOM-D2-002 | Mouth-open episode of 13 s (simulated stuck detection) | Episode discarded, no event, `model_degraded` set | N/R |
| TC-DOM-016 | L1 | DOM-D2-003 | One yawn event | D2 stays IDLE — **a single yawn must never alarm** | N/R |
| TC-DOM-017 | L1 | DOM-D2-003 | Two yawn events 100 s apart | D2 = ACTIVE on the second | N/R |
| TC-DOM-018 | L1 | DOM-D2-003 | Two yawn events 130 s apart | D2 stays IDLE — first has left the window | N/R |
| TC-DOM-019 | L1 | DOM-D2-004 | Three yawn events in 90 s | D2 = SEVERE | N/R |
| TC-DOM-020 | L1 | DOM-D2-004 | Two yawn events, one of 5.5 s | D2 = SEVERE via the single-duration path | N/R |
| TC-DOM-021 | L1 | DOM-D2-007 | D2 = ACTIVE, then send ACK | D2 remains ACTIVE; the yawn window is not cleared | N/R |
| TC-DOM-022 | L2 | AC-03 | Replay C-DROWSY held-out; match against `yawn` annotations | F1 ≥ 0.85 | N/R |

### 2.3 D3 Eye closure

| ID | Level | Verifies | Procedure | Pass criterion | Status |
|---|---|---|---|---|---|
| TC-DOM-023 | L1 | DOM-D3-002 | Closure of 400 ms (long normal blink) | D3 stays IDLE | N/R |
| TC-DOM-024 | L1 | DOM-D3-002 | Closure of 800 ms | D3 = ACTIVE at 800 ms ± 1 frame period | N/R |
| TC-DOM-025 | L1 | DOM-D3-002 | Closure of 1500 ms | D3 = SEVERE | N/R |
| TC-DOM-026 | L1 | DOM-D3-002 | Closure of 3000 ms | D3 = CRITICAL | N/R |
| TC-DOM-027 | L2 | DOM-D3-002 | Replay 30 min of C-BASE containing ≥ 500 normal blinks | Zero D3 ACTIVE declarations from blinking alone | N/R |
| TC-DOM-028 | L1 | DOM-D3-001 | Closure sequence with one interleaved frame at confidence 0.3 | Accumulator **holds** — neither resets nor increments; closure still declared at 800 ms of valid evidence | N/R |
| TC-DOM-029 | L1 | DOM-FLT-001 | Same 800 ms closure replayed at 10 FPS and at 3 FPS | ACTIVE declared at 800 ms of wall-clock in both cases (not after N frames) | N/R |
| TC-DOM-030 | L1 | DOM-D3-003 | Start pipeline, request PERCLOS at t = 20 s | PERCLOS reported INVALID (0xFF), contributes to no state | N/R |
| TC-DOM-031 | L1 | DOM-D3-004 | Synthetic stream giving PERCLOS = 0.09 | D3 ≥ ACTIVE | N/R |
| TC-DOM-032 | L1 | DOM-D3-004 | Synthetic stream giving PERCLOS = 0.16 | D3 ≥ SEVERE | N/R |
| TC-DOM-033 | L1 | DOM-D3-004 | From SEVERE, PERCLOS falls to 0.145 | D3 does **not** de-escalate (hysteresis 0.02) | N/R |
| TC-DOM-034 | L1 | DOM-D3-006 | Synthetic stream giving PERCLOS = 0.60 with no closure > 1 s | D3 reaches SEVERE but **never** CRITICAL | N/R |
| TC-DOM-035 | L1 | DOM-D3-005 | Sub-signal (a) = ACTIVE while (b) = SEVERE | Effective D3 = SEVERE (maximum) | N/R |
| TC-DOM-036 | L3 | DOM-D3-008 | Subject wears dark sunglasses | D3 = UNAVAILABLE, `model_degraded` set, system holds at L1. **D3 must not report IDLE** | N/R |
| TC-DOM-037 | L3 | SYS-FR-005 | Cover the camera lens for 5 s | `SENSOR_LOST` at 3 s; blue flashing indication; **buzzer silent** | N/R |
| TC-DOM-038 | L2 | AC-01 | Replay C-DROWSY held-out; match against `eye_closure` ≥ 1.5 s annotations | TPR ≥ 0.95 | N/R |
| TC-DOM-039 | L3 | SYS-FR-004 | Full darkness with IR illumination, subject performs scripted closures | D3 detects; `night_mode` flag set | N/R |
| TC-DOM-040 | L3 | SYS-ER-003 | Subject wears clear prescription glasses | D3 TPR degrades by ≤ 10 percentage points vs no glasses | N/R |

---

## 3. TC-FUS — Fusion and the alert ladder

| ID | Level | Verifies | Procedure | Pass criterion | Status |
|---|---|---|---|---|---|
| TC-FUS-001 | L1 | §6.1 | D1 ACTIVE only | Level = L1 | N/R |
| TC-FUS-002 | L1 | §6.1 | D2 ACTIVE only | Level = L1 | N/R |
| TC-FUS-003 | L1 | §6.1 | D3 ACTIVE only | Level = L1 | N/R |
| TC-FUS-004 | L1 | §6.2 | D1 ACTIVE **and** D2 ACTIVE, neither SEVERE | Level = L2 (multi-domain corroboration) | N/R |
| TC-FUS-005 | L1 | §6.1 | D2 SEVERE only | Level = L2 | N/R |
| TC-FUS-006 | L1 | DOM-000 | D1 SEVERE sustained 60 s, D2/D3 IDLE | Level reaches L2 and **never** L3 | N/R |
| TC-FUS-007 | L1 | DOM-000 | D2 SEVERE sustained 60 s, D1/D3 IDLE | Level reaches L2 and **never** L3 | N/R |
| TC-FUS-008 | L1 | §6.1 | D3 CRITICAL from L0 | Level goes directly to L3 (level skipping permitted only here) | N/R |
| TC-FUS-009 | L1 | §6.3 | L2 held for 10 s with no ACK | Level escalates to L3 at 10.0 s ± 1 frame | N/R |
| TC-FUS-010 | L1 | §6.3 | L2 held for 9 s then ACK | Level returns to L0; no escalation | N/R |
| TC-FUS-011 | L1 | DOM-FUS-001 | ACK at L2, then D1 ACTIVE 5 s later | L1 suppressed during refractory | N/R |
| TC-FUS-012 | L1 | DOM-FUS-001 | ACK at L2, then D3 SEVERE 5 s later | L2 raised — **refractory does not suppress L2** | N/R |
| TC-FUS-013 | L1 | DOM-FUS-002 / DOM-D3-007 | D3 CRITICAL, then ACK pressed repeatedly | Level stays L3; ACK has no effect | N/R |
| TC-FUS-014 | L1 | DOM-FUS-003 | Four ACKs within 10 min with no sustained L0 between | `ack_saturated` set; refractory disabled | N/R |
| TC-FUS-015 | L1 | DOM-FUS-005 | From L2, all domains clear for 4.9 s | No de-escalation | N/R |
| TC-FUS-016 | L1 | DOM-FUS-005 | From L2, all domains clear for 5.0 s | De-escalate to **L1**, not to L0 (one step at a time) | N/R |
| TC-FUS-017 | L1 | DOM-FUS-006 | At L3, all domains clear for 60 s | Level stays L3; no automatic exit | N/R |
| TC-FUS-018 | L1 | DOM-FLT-002 | Inject `SENSOR_LOST` while at L0 | Level ≥ L1; never mapped to L0 | N/R |
| TC-FUS-019 | L2 | §6.6 | Replay the worked timeline of spec 03 §6.6 as a synthetic corpus | Level timeline matches the documented sequence exactly | N/R |
| TC-FUS-020 | L2 | AC-02 | Replay all of C-BASE held-out split | ≤ 1.0 L1-or-above entries per hour | N/R |
| TC-FUS-021 | L4 | SYS-FR-021 | Drive each level via `can_inject.py`, observe actuators | Actuation matches the VEH-030 table exactly for every level | N/R |
| TC-FUS-022 | L4 | VEH-031 | Hold L1 for 60 s | Buzzer emits exactly 2 pulses then stays silent | N/R |
| TC-FUS-023 | L4 | VEH-032 | Inject `SENSOR_LOST` | Blue 1 Hz flash, buzzer silent, distinguishable from any drowsiness level | N/R |
| TC-FUS-024 | L4 | SYS-FR-022 | Cycle through all levels and faults | Status LED colour/pattern unique and correct for each | N/R |

---

## 4. TC-CAN — Interface

| ID | Level | Verifies | Procedure | Pass criterion | Status |
|---|---|---|---|---|---|
| TC-CAN-001 | L3 | CAN-001 | Measure bus resistance, power off | 60 Ω ± 5 Ω | N/R |
| TC-CAN-002 | L3 | §1 | Scope CAN_H/CAN_L levels | Recessive ≈2.5 V both; dominant ≈3.5 V / 1.5 V | N/R |
| TC-CAN-003 | L3 | §1 | Measure bit time on the scope, both nodes independently | 2.00 µs ± 1 % | N/R |
| TC-CAN-004 | L1 | CAN-070 | Run `crc_vectors.csv` against both node implementations | Identical output for every vector | N/R |
| TC-CAN-005 | L1 | §3 | Encode/decode round-trip of `DMS_STATUS` across all field extremes | Bit-exact round trip; no field aliasing | N/R |
| TC-CAN-006 | L1 | CAN-010 | Decode a frame with a known 16-bit field on both nodes | Both interpret little-endian identically | N/R |
| TC-CAN-007 | L4 | CAN-011 | Inject a frame with a corrupted CRC | Frame discarded; supervisor **not** refreshed | N/R |
| TC-CAN-008 | L4 | CAN-011 | Inject a frame with DLC = 7 | Frame discarded | N/R |
| TC-CAN-009 | L4 | CAN-012 | Inject a `seq` jump of +3 | Frame accepted, `frames_lost` incremented, no failsafe | N/R |
| TC-CAN-010 | L4 | CAN-014 | Send `alert_level = L0` with `calib_done = 0` and attempt to arm | VCS remains disarmed | N/R |
| TC-CAN-011 | L4 | CAN-040 | Press ACK; count frames on the bus | Exactly 3 frames, same `event_seq`, DMS registers one ACK | N/R |
| TC-CAN-012 | L4 | CAN-040 | Drop 2 of the 3 ACK frames (bus fault injection) | ACK still registered exactly once | N/R |
| TC-CAN-013 | L4 | CAN-051 | Inject `0x080` with `magic = 0x00` | Ignored; vehicle continues | N/R |
| TC-CAN-014 | L4 | CAN-050 | Inject valid `0x080` at full speed | Motor outputs disabled within one control cycle (≤ 10 ms), state `ESTOP` | N/R |
| TC-CAN-015 | L4 | SYS-PR-005 / CAN-066 | 30 min run with bus-load and error-counter logging | Load ≤ 5 %; zero bus-off events; TX/RX error counters remain 0 | N/R |

---

## 5. TC-SAF — Safety and failsafe

**These are the cases that decide whether the system is demo-ready.** Run them all, on the wheel
stand, before anything touches the floor.

| ID | Level | Verifies | Procedure | Pass criterion | Status |
|---|---|---|---|---|---|
| TC-SAF-001 | L3 | SYS-SR-001 / VEH-010 | Power on with a throttle command already applied | Motors do not turn; state `INIT` → `DISARMED` | N/R |
| TC-SAF-002 | L3 | VEH-011 | Attempt to arm with `calib_done = 0` | Arming refused | N/R |
| TC-SAF-003 | L4 | CAN-063 | Running at L0, 80 % duty; stop transmitting `DMS_STATUS` | **Vehicle does not continue at L0.** `LINK_LOST` entered | N/R |
| TC-SAF-004 | L4 | CAN-061 / AC-09 | As above; measure with LA from last frame to speed-cap change | `LINK_LOST` at 300 ms, +0/−50 ms | N/R |
| TC-SAF-005 | L4 | CAN-062 / AC-10 | Keep the link down | Safe stop begins at 1000 ms, +100/−0 ms | N/R |
| TC-SAF-006 | L4 | CAN-064 | Restore the link after 2 s at the pre-fault level | Requires 5 consecutive valid frames; resumes at the **newly received** level, not the pre-fault one | N/R |
| TC-SAF-007 | L4 | VEH-012 / SYS-FR-033 | After a completed safe stop, send `alert_level = L0` continuously | Vehicle stays `STOPPED`; no motion | N/R |
| TC-SAF-008 | L4 | VEH-043 | After safe stop, press operator re-arm | Vehicle returns to `ARMED_IDLE`; motion possible again only with throttle | N/R |
| TC-SAF-009 | L4 | VEH-044 | Enter L3, then send L0 300 ms into the ramp | Ramp completes; no re-acceleration | N/R |
| TC-SAF-010 | L3 | VEH-052 / VEH-053 | Force `control_task` to stall (test hook) | WWDT resets the node; comes up `DISARMED`; `fault_watchdog_reset` set for 5 s; ERROR logged | N/R |
| TC-SAF-011 | L3 | VEH-054 | Stall a wheel by hand at 60 % duty | `fault_driver` within 500 ms; driver stage disabled; state `FAULT` | N/R |
| TC-SAF-012 | L3 | VEH-055 | Drop the motor rail below `V_UNDERVOLT` for 300 ms | `fault_undervoltage`; state `FAULT`; no erratic motion | N/R |
| TC-SAF-013 | L3 | SYS-SR-005 / VEH-074 | Press the physical e-stop at full speed **with the MCU halted in a debugger** | Motors stop. This path must not depend on firmware | N/R |
| TC-SAF-014 | L4 | VEH-056 | From every fault state, attempt every input | No input transitions directly to `RUN` | N/R |
| TC-SAF-015 | L4 | SYS-SR-004 | Inject each fault while at each level (matrix) | Resulting state is never more permissive than the pre-fault state | N/R |
| TC-SAF-016 | L4 | VEH-042 | Trigger a safe stop 10× and observe heading | No spin; heading deviation ≤ 15° per run | N/R |
| TC-SAF-017 | L3 | DEV-045 | `SIGTERM` the DMS-AP process | Publishes L0 + `calib_done = 0`, VCS disarms, clean exit | N/R |

---

## 6. TC-VEH — Vehicle control

| ID | Level | Verifies | Procedure | Pass criterion | Status |
|---|---|---|---|---|---|
| TC-VEH-001 | L3 | VEH-022 | Sweep setpoint 0→100 % in 1 % steps, note where wheels start | Record `MIN_MOVE_DUTY`; update `thresholds.yaml` | N/R |
| TC-VEH-002 | L3 | VEH-020 | Inject each level, measure `duty_out` at 100 % throttle | 100/80/50/0 % ± 2 % for L0/L1/L2/L3 | N/R |
| TC-VEH-003 | L3 | VEH-020 | Inject `LINK_LOST` | Cap = 30 % | N/R |
| TC-VEH-004 | L4 | VEH-021 | Step L0→L2 at full throttle; capture duty on LA | Rate of change ≤ 40 %/s; no lurch or skid | N/R |
| TC-VEH-005 | L3 | VEH-023 | Command differential (left 60 %, right 30 %) at L1 | Both scaled by the same 80 % cap; ratio preserved | N/R |
| TC-VEH-006 | L4 | VEH-040 / AC-11 | Trigger L3 at full speed; LA from L3 frame to motor-enable de-assert | 2.0 s ± 0.1 s; ramp 1500 ms then brake 500 ms | N/R |
| TC-VEH-007 | L4 | VEH-041 / OI-05-06 | Encoder or 240 fps video of the safe stop, 3 runs | Deceleration profile recorded in spec 08; monotonic, no reversal | N/R |
| TC-VEH-008 | L3 | VEH-002 | Measure PWM frequency on the LA | 20 kHz (TB6612FNG) or 8 kHz (L298N), ± 5 % | N/R |
| TC-VEH-009 | L3 | VEH-035 | LA: CAN RX done (ch2) to actuator change (ch3) | ≤ 20 ms | N/R |
| TC-VEH-010 | L3 | VEH-061 / AC-08 | LA on ch4 for 60 s | Control-tick jitter ≤ ±1 ms | N/R |
| TC-VEH-011 | L3 | VEH-064 | 30 min run, then read stack high-water marks | ≥ 50 % headroom on every task | N/R |
| TC-VEH-012 | L3 | VEH-072 | Switch the fan relay 50× while logging MCU resets | Zero resets, zero CAN errors | N/R |
| TC-VEH-013 | L3 | VEH-073 | Start all four motors simultaneously at 100 %; scope the motor rail | Rail sag ≤ 0.5 V; no brownout | N/R |
| TC-VEH-014 | L3 | VEH-033 / AC-15 | SPL meter at 1 m, L3 alert active | ≤ 85 dB(A) | N/R |
| TC-VEH-015 | L3 | VEH-014 | Force an invalid state value via test hook | Logged and transitions to `FAULT` | N/R |

---

## 7. TC-PERF — Performance

| ID | Level | Verifies | Procedure | Pass criterion | Status |
|---|---|---|---|---|---|
| TC-PERF-001 | L4 | SYS-PR-001 / AC-05 | LA ch0→ch3 over ≥ 500 events | P95 ≤ 200 ms; report P50/P95/max | N/R |
| TC-PERF-002 | L4 | SYS-PR-002 | Same capture, correlated with frame timestamps | Frame-quantisation contribution reported **separately** from pipeline latency | N/R |
| TC-PERF-003 | L3 | SYS-PR-003 / AC-06 | Count ch0 edges over 60 s | ≥ 8 FPS | N/R |
| TC-PERF-004 | L3 | SYS-PR-004 / AC-07 | 30 min continuous run; FPS at min 1 and min 30; log SoC temperature | FPS(30) ≥ 0.8 × FPS(1) | N/R |
| TC-PERF-005 | L3 | — | Inference-time distribution over 1000 frames | Report P50/P95/max; P95 ≤ 100 ms | N/R |
| TC-PERF-006 | L3 | SYS-PR-008 | Monitor RSS over a 30 min run | ≤ 512 MB, and flat (no growth trend) | N/R |
| TC-PERF-007 | L3 | SYS-PR-008 | Link map of the VCS build | ≥ 20 % RAM and ≥ 20 % flash free | N/R |
| TC-PERF-008 | L3 | SYS-ER-004 | Bench supply readings: idle, peak, 60 s average, both rails | Recorded in spec 08 | N/R |
| TC-PERF-009 | L3 | SYS-ER-005 | Power on to first valid `DMS_STATUS` with `calib_done = 1` | ≤ 30 s | N/R |
| TC-PERF-010 | L4 | AC-12 / AC-13 | 30 min continuous system run with full logging | Zero unexpected states; zero bus-off; zero watchdog resets | N/R |
| TC-PERF-011 | L3 | TP-002 | Measure FPS with GPIO markers compiled out vs in | Overhead recorded; < 2 % | N/R |
| TC-PERF-012 | L2 | TP-031 | Time the full L1+L2 suite in CI | ≤ 3 min | N/R |

---

## 8. TC-ROB — Robustness and environment

| ID | Level | Verifies | Procedure | Pass criterion | Status |
|---|---|---|---|---|---|
| TC-ROB-001 | L3 | SYS-ER-002 | Replay C-ADVERSE bright-sun/glare clips | No crash; degradation declared via `model_degraded`, not silently absorbed | N/R |
| TC-ROB-002 | L3 | SYS-ER-002 | 5 lux with IR illumination | D3 functional; `night_mode` set; TPR reported separately for this condition | N/R |
| TC-ROB-003 | L3 | SYS-ER-003 | Clear glasses (covered by TC-DOM-040) | See TC-DOM-040 | N/R |
| TC-ROB-004 | L3 | DOM-D3-008 | Sunglasses (covered by TC-DOM-036) | See TC-DOM-036 | N/R |
| TC-ROB-005 | L3 | — | Subject wearing a face mask | D2 marked degraded; D3 unaffected | N/R |
| TC-ROB-006 | L3 | — | Subject leaves the frame entirely for 10 s | `SENSOR_LOST` at 3 s; recovers within 1 s of return | N/R |
| TC-ROB-007 | L3 | — | Two faces in frame (passenger leans in) | Deterministic subject selection (largest/most central); no oscillation between faces | N/R |
| TC-ROB-008 | L4 | — | Vibrate the chassis (motors at 100 %) during detection | No CAN errors; no spurious level changes | N/R |
| TC-ROB-009 | L3 | — | Unplug and replug the camera mid-run | `SENSOR_LOST`, then clean recovery; no crash, no stale frames | N/R |
| TC-ROB-010 | L4 | — | Brown out the logic supply to 4.2 V for 100 ms | Either clean operation or clean reset to `DISARMED`; never uncommanded motion | N/R |
| TC-ROB-011 | L3 | — | Fill the log partition to 100 % | Logging degrades gracefully; detection and actuation unaffected | N/R |
| TC-ROB-012 | L4 | — | 3-hour soak run | No memory growth, no FPS decay beyond AC-07, no state anomaly | N/R |

---

## 9. TC-DEV — Process and build

| ID | Level | Verifies | Procedure | Pass criterion | Status |
|---|---|---|---|---|---|
| TC-DEV-001 | L1 | DEV-003 | Re-run `shared/icd/generate.py` in CI | Zero diff against committed artefacts | N/R |
| TC-DEV-002 | L1 | DEV-051 | Run `tools/check_thresholds.py` | Spec 03 §8 table equals `thresholds.yaml` exactly | N/R |
| TC-DEV-003 | L3 | DEV-071 | Read the boot banner from both nodes | Version, git SHA, `+dirty` state and timestamp present and correct | N/R |
| TC-DEV-004 | L3 | DEV-083 | Run the deploy script and read back | Deployed SHA matches the intended SHA | N/R |
| TC-DEV-005 | L1 | DEV-052 | Parse `thresholds.yaml` | Every entry has a non-empty `rationale:` field | N/R |
| TC-DEV-006 | L3 | DEV-084 | Query model name and SHA-256 via boot log and CAN diagnostics | Both report the same active model | N/R |

---

## 10. Traceability summary

| Requirement group | Covering cases |
|---|---|
| SYS-AR (architecture, privacy) | TC-ARC-001…006 |
| SYS-FR (functional) | TC-DOM-*, TC-FUS-*, TC-VEH-* |
| SYS-PR (performance) | TC-PERF-001…012 |
| SYS-IR (interface) | TC-CAN-001…015 |
| SYS-SR (safety) | TC-SAF-001…017 |
| SYS-ER (environment) | TC-ROB-001…012, TC-PERF-008/009 |
| DOM-D1 | TC-DOM-001…010 |
| DOM-D2 | TC-DOM-011…022 |
| DOM-D3 | TC-DOM-023…040 |
| DOM-FUS / DOM-FLT | TC-FUS-001…024 |
| CAN-* | TC-CAN-001…015, TC-SAF-003…006 |
| VEH-* | TC-VEH-001…015, TC-SAF-001…016 |
| DEV-* | TC-DEV-001…006 |

**Uncovered requirements are a defect in this catalogue.** Before the acceptance gate, run
`tools/check_traceability.py`, which parses requirement IDs out of specs 01/03/04/05 and this file
and reports any requirement with no covering case.

---

## Revision history

| Rev | Date | Author | Change |
|---|---|---|---|
| 0.1 | 2026-08-10 | ML_IoT_Love50 | Initial baseline — 0 of 130 cases executed |
