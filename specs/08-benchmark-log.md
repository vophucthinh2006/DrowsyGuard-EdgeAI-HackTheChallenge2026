# 08 — Benchmark Log

**Document:** DG-SPEC-08 · Rev 0.1 · 2026-08-10 · **NO MEASUREMENTS RECORDED YET**
**Method definitions:** [06 §4](06-test-plan.md#4-instrumentation-and-measurement-methods)

---

## 1. Rules for this document

**BM-001** — A number appears here only if it was **measured**. Targets, budgets and estimates live
in specs 01 and 06. Mixing the two is how a budget becomes a quoted result without anyone deciding
that it should.

**BM-002** — Every entry SHALL cite a **run ID** with a matching artefact directory
`docs/benchmarks/<run-id>/` containing the raw capture, the log, the git SHA, the SHA-256 of
`thresholds.yaml`, and the environment record (§3).

**BM-003** — No entry SHALL come from a `+dirty` build
([02 DEV-072](02-development-standards.md#8-build)).

**BM-004** — Latency and timing are reported as **P50 / P95 / max** with the sample count. Never as
a bare mean.

**BM-005** — Detection metrics are reported as a **pair**: true-positive rate **and** the
false-alarm rate on C-BASE at the same operating point. A TPR quoted alone SHALL be treated as an
incomplete result.

**BM-006** — When a result misses its target, it is recorded **as measured**, with a note on the
suspected cause. Re-running until a good number appears and recording only that one is data
fabrication. If several runs were taken, record the distribution or the worst case, and say how
many runs there were.

**BM-007** — Every result SHALL name the person who took it. Not for blame — so that the person who
knows what the rig looked like that day can be asked.

---

## 2. Run ID convention

```
BM-YYYY-MM-DD-NN        e.g. BM-2026-08-14-03  (third run of 14 August)
```

---

## 3. Environment record template

Copy into each artefact directory as `environment.md`.

```yaml
run_id:            BM-2026-08-__-__
date_utc:
operator:
build_sha:                     # git rev-parse --short HEAD
build_dirty:       false       # MUST be false for a recorded result
thresholds_sha256:
model_file:                    # models/xxx-v0.3.tflite
model_sha256:
corpus:                        # C-BASE / C-DROWSY / C-ADVERSE / live
corpus_split:                  # tuning | held-out
camera:                        # model, resolution, exposure mode
illumination_lux:
ambient_temp_c:
motor_driver:                  # TB6612FNG | L298N
motor_supply_v:
logic_supply_v:
instrument:                    # logic analyser model, sample rate
sample_rate:
notes:
```

---

## 4. Latency and timing

### 4.1 End-to-end pipeline latency (TC-PERF-001)

**Method:** logic analyser, ch0 rising (inference decision) → ch3 rising (actuator change).
Budget: 160 ms. Requirement: P95 ≤ 200 ms.

| Run ID | Date | Build | Samples | P50 (ms) | P95 (ms) | Max (ms) | Target met | Notes |
|---|---|---|---:|---:|---:|---:|---|---|
| _pending_ | | | | | | | | |

### 4.2 Stage breakdown (TC-PERF-005)

| Run ID | Capture+convert | Pre-process | Inference P50 | Inference P95 | Post+fusion | AP→RT | CAN | VCS actuate | Sum |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| _budget_ | 25 | 10 | 80 | — | 10 | 10 | 5 | 20 | **160** |
| _pending_ | | | | | | | | | |

### 4.3 Time-to-alert as the driver experiences it (TC-PERF-002)

Dwell + frame quantisation + pipeline. **These are the numbers to quote publicly**
([03 §5.1](03-drowsiness-domain-spec.md#51-sub-signal-a--continuous-closure--microsleep)).

| Transition | Predicted worst case | Run ID | Measured P50 | Measured P95 | Notes |
|---|---:|---|---:|---:|---|
| D3 ACTIVE → L1 | ≈ 1.06 s | _pending_ | | | |
| D3 SEVERE → L2 | ≈ 1.76 s | _pending_ | | | |
| D3 CRITICAL → L3 | ≈ 3.26 s | _pending_ | | | |
| D1 ACTIVE → L1 | ≈ 2.26 s | _pending_ | | | |

### 4.4 Control-loop jitter (TC-VEH-010)

| Run ID | Duration | Nominal | Min (ms) | Max (ms) | Std dev | ≤ ±1 ms | Notes |
|---|---|---:|---:|---:|---:|---|---|
| _pending_ | | 10.0 | | | | | |

---

## 5. Inference throughput

### 5.1 Sustained FPS (TC-PERF-003)

Target ≥ 8 FPS, goal 10 FPS.

| Run ID | Model | Input res | Quant | Backend | FPS (60 s avg) | Inference P95 (ms) | CPU % | Notes |
|---|---|---|---|---|---:|---:|---:|---|
| _pending_ | | | | | | | | |

### 5.2 Thermal behaviour (TC-PERF-004)

Requirement: FPS(min 30) ≥ 0.8 × FPS(min 1).

| Run ID | FPS min 1 | FPS min 10 | FPS min 20 | FPS min 30 | Ratio | SoC temp start/end (°C) | Pass |
|---|---:|---:|---:|---:|---:|---|---|
| _pending_ | | | | | | | |

### 5.3 Resource footprint (TC-PERF-006/007)

| Run ID | AP RSS peak (MB) | AP RSS drift over 30 min | VCS flash used / free | VCS RAM used / free | Min stack headroom |
|---|---:|---|---|---|---|
| _pending_ | | | | | |

---

## 6. Detection quality

**Reported on the held-out split only** ([06 TP-012](06-test-plan.md#5-test-corpora)). Every row
carries its paired false-alarm rate.

### 6.1 Headline operating point

| Run ID | Corpus rev | Thresholds SHA | AC-01 microsleep TPR | AC-02 FA/hour | AC-03 yawn F1 | AC-04 distraction TPR | All met |
|---|---|---|---:|---:|---:|---:|---|
| _pending_ | | | | | | | |

### 6.2 Per-domain detail

| Run ID | Domain | Events in corpus | TP | FN | FP | TPR | Precision | F1 | Notes |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| _pending_ | D1 distraction | | | | | | | | |
| _pending_ | D2 yawn | | | | | | | | |
| _pending_ | D3 closure ≥ 1.5 s | | | | | | | | |

### 6.3 False alarms on C-BASE, broken down by cause

The most useful table in this document. Where the false alarms come from tells you which threshold
is wrong; the total alone does not.

| Run ID | Total FA | per hour | Talking | Laughing | Drinking | Mirror check | Glasses reflection | Glare | Other |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| _pending_ | | | | | | | | | |

### 6.4 Condition breakdown (TC-ROB-001…005)

Aggregate metrics hide the condition where the system fails. Report every condition separately.

| Run ID | Condition | Microsleep TPR | FA/hour | D3 availability % | Notes |
|---|---|---:|---:|---:|---|
| _pending_ | Daylight, no glasses | | | | |
| _pending_ | Daylight, clear glasses | | | | |
| _pending_ | Direct sun / glare | | | | |
| _pending_ | Dark cabin + IR | | | | |
| _pending_ | Sunglasses | n/a | | | D3 SHALL be UNAVAILABLE, not IDLE |
| _pending_ | Face mask | | | | D2 degraded expected |

### 6.5 Ground-truth quality (TP-013)

| Corpus | Events double-annotated | Inter-annotator agreement | Main disagreement class |
|---|---:|---:|---|
| C-DROWSY | _pending_ | | |
| C-BASE | _pending_ | | |

> Detector accuracy cannot exceed annotation agreement. Record this before quoting any TPR.

---

## 7. Interface

### 7.1 CAN physical (TC-CAN-001…003)

| Run ID | Bus resistance (Ω) | Recessive (V) | Dominant H/L (V) | Measured bit time (µs) | Node | Pass |
|---|---:|---:|---|---:|---|---|
| _pending_ | | | | | DMS | |
| _pending_ | | | | | VCS | |

### 7.2 Bus health over a 30 min run (TC-CAN-015)

| Run ID | Bus load % | TX error count | RX error count | Bus-off events | Frames lost (seq gaps) | CRC failures |
|---|---:|---:|---:|---:|---:|---:|
| _pending_ | | | | | | |

### 7.3 Timeout behaviour (TC-SAF-004/005)

| Run ID | Link break → `LINK_LOST` (ms) | Target 300 +0/−50 | Link break → safe stop start (ms) | Target 1000 +100/−0 | Runs | Pass |
|---|---:|---|---:|---|---:|---|
| _pending_ | | | | | | |

---

## 8. Vehicle

### 8.1 Drivetrain characterisation (TC-VEH-001, OI-05-01/02)

| Run ID | `MIN_MOVE_DUTY` measured | Stall current per motor (A) | 4-motor start surge (A) | Rail sag at surge (V) | Free-run current (A) |
|---|---:|---:|---:|---:|---:|
| _pending_ | | | | | |

### 8.2 Speed governing (TC-VEH-002/004)

| Run ID | L0 duty % | L1 duty % | L2 duty % | Cap ramp rate (%/s) | Lurch observed | Pass |
|---|---:|---:|---:|---:|---|---|
| _pending_ | | | | | | |

### 8.3 Safe stop (TC-VEH-006/007, AC-11)

| Run ID | L3 frame → duty 0 (ms) | → motors disabled (ms) | Target 2000 ± 100 | Peak decel (m/s²) | Heading deviation (°) | Runs | Pass |
|---|---:|---:|---|---:|---:|---:|---|
| _pending_ | | | | | | | |

Deceleration profile artefact (encoder trace or 240 fps video frame analysis) filed at:
`docs/benchmarks/<run-id>/decel_profile.csv`

### 8.4 Alerts (TC-VEH-008/014)

| Run ID | PWM freq (kHz) | Driver used | L1 SPL dB(A) @1 m | L2 SPL | L3 SPL | ≤ 85 dB(A) | Audible PWM whine |
|---|---:|---|---:|---:|---:|---|---|
| _pending_ | | | | | | | |

---

## 9. Power

| Run ID | Rail | Idle (mA) | Detecting (mA) | Peak (mA) | 60 s avg (mA) | Notes |
|---|---|---:|---:|---:|---:|---|
| _pending_ | Logic 5 V | | | | | |
| _pending_ | Motor 7.4 V | | | | | |

Total system average power: _pending_ W

---

## 10. Acceptance-criteria scoreboard

Updated at every acceptance run. This table is what gets shown to a judge who asks "does it work?"

| # | Criterion | Target | Measured | Run ID | Status |
|---|---|---|---|---|---|
| AC-01 | Microsleep TPR | ≥ 0.95 | — | — | ⬜ |
| AC-02 | False alarms / hour | ≤ 1.0 | — | — | ⬜ |
| AC-03 | Yawn F1 | ≥ 0.85 | — | — | ⬜ |
| AC-04 | Distraction TPR | ≥ 0.90 | — | — | ⬜ |
| AC-05 | Pipeline latency P95 | ≤ 200 ms | — | — | ⬜ |
| AC-06 | Sustained FPS | ≥ 8 | — | — | ⬜ |
| AC-07 | Thermal retention | ≥ 80 % | — | — | ⬜ |
| AC-08 | Control-loop jitter | ≤ ±1 ms | — | — | ⬜ |
| AC-09 | `LINK_LOST` timing | 300 +0/−50 ms | — | — | ⬜ |
| AC-10 | Safe stop on timeout | 1000 +100/−0 ms | — | — | ⬜ |
| AC-11 | Safe-stop duration | 2.0 s ± 0.1 s | — | — | ⬜ |
| AC-12 | Unexpected states in 30 min | 0 | — | — | ⬜ |
| AC-13 | Bus-off events in 30 min | 0 | — | — | ⬜ |
| AC-14 | Open ⚠️ ASSUMPTION items | 0 | 5 open | — | ⬜ |
| AC-15 | Sound pressure | ≤ 85 dB(A) | — | — | ⬜ |

**Current status: 0 / 15 criteria measured.** This is the accurate state of the project at Rev 0.1
and SHALL be presented as such until measurements exist.

---

## 11. Anomaly register

Every unexpected observation, including ones later explained away. The ones written down are the
ones that get fixed.

| # | Date | Run ID | Observation | Reproducible | Root cause | Resolution | Test case added |
|---|---|---|---|---|---|---|---|
| _none yet_ | | | | | | | |

---

## 12. Numbers currently quoted in external material

Every figure used in the pitch deck, the script or any submission, and whether it is backed by a
measurement here. **A figure with no run ID is a claim, and SHALL be spoken as a target.**

| Figure | Where used | Type | Backing run ID | Action |
|---|---|---|---|---|
| "under 200 ms end-to-end" | deck slide 10, script 0:55 | **Target** | none | Say "our target is"; replace with the measured P95 once AC-05 is run |
| "5–10 FPS is enough" | script 3:00 | Design argument | none | Supported by PERCLOS window reasoning ([03 §5.2](03-drowsiness-domain-spec.md#52-sub-signal-b--perclos-trend)); still say "we expect" until AC-06 |
| "under 100 ms on the MCU" | deck slide 11 | **Target** | none | Measurable via TC-VEH-009 (budget 20 ms) — measure and quote the real number |
| "3 seconds = 100 metres" | script 0:08, 4:40 | Arithmetic | n/a | Correct at 120 km/h; at the 90 km/h stated in the script, 3 s = **75 m**. ⚠️ **Fix the script or the speed before recording** |
| "nano INT8 at ≈ N FPS" | deck slide 20 placeholder | **Unfilled** | none | Fill from AC-06 or delete the slide. Do not ship a placeholder |
| "TRL 4" | deck slide 16 | Self-assessment | n/a | Defensible; becomes TRL 5–6 only once AC-01…AC-15 are measured on the loaned hardware |

**BM-010** — This table SHALL be reviewed before any recording, submission or presentation. It
exists because the fastest way to lose a technical judge is to state a budget in the confident tone
of a measurement.

> ⚠️ **The "3 seconds = 100 metres" discrepancy in row 4 is live and unresolved.** 100 m in 3 s is
> 120 km/h; the script says ninety kilometres an hour, which gives 75 m. Either change the speed to
> 120 km/h or change the distance to 75 m. A judge with a calculator will check this one.

---

## Revision history

| Rev | Date | Author | Change |
|---|---|---|---|
| 0.1 | 2026-08-10 | ML_IoT_Love50 | Template established; zero measurements recorded |
