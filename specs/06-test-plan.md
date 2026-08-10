# 06 — Test Plan

**Document:** DG-SPEC-06 · Rev 0.1 · 2026-08-10 · DRAFT
**Companion documents:** [07 — Test Case Catalogue](07-test-cases.md) · [08 — Benchmark Log](08-benchmark-log.md)

---

## 1. Objective and philosophy

The purpose of testing here is not to demonstrate that DrowsyGuard works. It is to find out
**where it stops working**, before a judge does.

Three principles follow from that:

**Every claim gets a number and a method.** "It detects microsleeps reliably" is not a result.
"TPR 0.96 (48/50 annotated closure events ≥ 1.5 s), at 0.7 false alarms/hour on the baseline
corpus, build `a1b2c3d`, run `BM-2026-08-14-03`" is a result. Sections 6 and 7 define the method
for every number the project will quote.

**Detection quality and timing are measured separately.** A detector can be accurate and late.
Merging accuracy and latency into one "it works" claim hides whichever one is worse.

**Failure paths are tested at least as hard as success paths.** The success path is what gets
exercised a hundred times a day during development; it is not where the bugs survive. Cable pulled,
camera covered, battery sagging, Linux side killed — those are tested deliberately, in
[07 §TC-SAF](07-test-cases.md#5-tc-saf--safety-and-failsafe), and a build that has not passed
them is not demo-ready.

---

## 2. Test levels

| Level | Name | Scope | Where it runs | Gate |
|---|---|---|---|---|
| **L1** | Unit | One module, no hardware | CI, every PR | Merge |
| **L2** | Corpus replay | Domain + fusion logic against annotated recordings | CI, every PR | Merge |
| **L3** | Node integration | One node, real hardware, stimulated inputs | Bench, on demand | Daily |
| **L4** | System integration | Both nodes, real CAN, real motors | Bench rig | Daily |
| **L5** | Acceptance | Full scripted scenarios, end to end | Demo rig | Once, before demo |

**Level 2 is the highest-value test in the project.** Because
[DEV-042](02-development-standards.md#5-coding-standards--application-python) makes the domain and
fusion code pure — observations and timestamps in, states out — a 30-minute annotated recording
replays through the exact production decision logic in under a second, deterministically, with no
camera and no board. Every threshold change is regression-tested against every recording ever made,
on every commit. This is the mechanism that stops "tuned it until the demo worked" from silently
destroying the false-alarm rate.

---

## 3. Test environment

### 3.1 Bench rig

```
   ┌── Logic analyser (8 ch) ──────────────────────────────┐
   │  ch0  DMS  GPIO_INFERENCE_DONE   (rising = decision made)
   │  ch1  DMS  GPIO_CAN_TX_START
   │  ch2  VCS  GPIO_CAN_RX_DONE
   │  ch3  VCS  GPIO_ACTUATOR_CHANGE  (rising = alert output changed)
   │  ch4  VCS  GPIO_CONTROL_TICK     (100 Hz loop marker)
   │  ch5  VCS  motor enable
   │  ch6/7 CAN_H / CAN_L (protocol decode)
   └───────────────────────────────────────────────────────┘

   Bench supply (logic) ── 5 V, current display
   Bench supply (motor) ── 6–7.4 V, current display, isolatable
   Chassis on a wheel stand — wheels free, vehicle stationary
   Stimulus: monitor at fixed distance playing corpus clips to the camera
```

**TP-001** — Latency SHALL be measured with the logic analyser using the GPIO markers above, not
with software timestamps on either node. Two nodes with unsynchronised clocks cannot measure a
cross-node interval; the scope can.

**TP-002** — The GPIO markers SHALL be compiled into **both** debug and release builds, and their
overhead SHALL be measured once and recorded. Instrumentation that only exists in debug builds
measures a program that is not the one being shipped.

**TP-003** — All motion testing SHALL be done with the chassis on a wheel stand until
[07 TC-SAF](07-test-cases.md#5-tc-saf--safety-and-failsafe) passes completely. A safe-stop bug on
the floor is a vehicle that drives into something.

### 3.2 Stimulus method

| Method | Used for | Repeatable? |
|---|---|---|
| **Corpus replay into the pipeline** (`ReplayBackend`) | Domain logic, fusion, thresholds | Fully deterministic |
| **Monitor playback to the camera** | Full optical path incl. exposure, IR, glare | Repeatable within lighting tolerance |
| **Live human subject, scripted actions** | Realism check, UX, acceptance | Not repeatable — never used to produce a quoted metric |
| **CAN frame injection** (`tools/can_inject.py`) | All VCS behaviour, without any camera | Fully deterministic |

**TP-004** — No number quoted in [08](08-benchmark-log.md) SHALL come from a live human subject
session. Live sessions are for finding problems, corpus runs are for measuring.

---

## 4. Instrumentation and measurement methods

Each metric below has exactly one defined method. Results reported by any other method SHALL say so.

| Metric | Method | Instrument |
|---|---|---|
| **Pipeline latency** | Δt from ch0 rising (decision) to ch3 rising (actuator), P50/P95/max over ≥ 500 events | Logic analyser |
| **Frame-to-decision latency** | Δt from camera frame timestamp to ch0 rising | Software + LA cross-check |
| **Time-to-alert (user-visible)** | Δt from the annotated ground-truth event start in the corpus clip to ch3 rising | LA + corpus timeline |
| **Achieved FPS** | Count of ch0 rising edges per second, averaged over 60 s | Logic analyser |
| **Inference time** | Software timer around the backend call, P50/P95 | Software, reported alongside FPS |
| **Control-loop jitter** | Interval statistics of ch4 over ≥ 60 s | Logic analyser |
| **CAN timeout response** | Δt from last CAN frame on ch6/7 to ch3 / ch5 change | LA with protocol decode |
| **Safe-stop duration** | Δt from L3 frame on the bus to motor enable de-assert on ch5 | Logic analyser |
| **Deceleration profile** | Wheel encoder or high-speed video of a marked wheel, 3 runs | Encoder / 240 fps video |
| **TPR / FPR** | Corpus replay against the annotation file, per-event matching | `tools/eval_corpus.py` |
| **False alarms per hour** | Count of L1+ entries over the baseline corpus, normalised to 1 h | `tools/eval_corpus.py` |
| **Power draw** | Bench supply current display, idle / peak / 60 s average | Bench supply |
| **Thermal throttle** | FPS at minute 1 vs minute 30 of a continuous run + `/sys/class/thermal` log | Software |
| **Sound pressure** | SPL meter, A-weighted, 1 m on axis | SPL meter |
| **Stack headroom** | `uxTaskGetStackHighWaterMark` after a 30 min run | Software |

**TP-005** — Latency SHALL always be reported as **P50 / P95 / max**, never as a mean. A mean hides
the tail, and the tail is what a drowsy driver experiences.

**TP-006** — Every metric SHALL be reported with the build SHA and the SHA-256 of
`thresholds.yaml`. A number without a configuration is not reproducible.

---

## 5. Test corpora

**TP-010** — Three corpora SHALL be built and version-controlled (video via git-lfs, annotations as
plain CSV).

| Corpus | Content | Duration target | Purpose |
|---|---|---|---|
| **C-BASE** — baseline | Alert subjects, normal driving behaviour: blinking, talking, mirror checks, drinking, adjusting radio, laughing, glasses on/off | ≥ **60 min** total | **False-alarm measurement.** This is the corpus that decides whether the product is usable |
| **C-DROWSY** — positive | Acted and (where available) genuine drowsiness: long closures, microsleeps, real yawns, head nods, gradual PERCLOS rise | ≥ **30 min** | TPR measurement |
| **C-ADVERSE** — robustness | Darkness + IR, direct sun/glare, clear glasses, sunglasses, face partially occluded, camera shake, subject leaves frame | ≥ **20 min** | Degradation and fault behaviour |

**TP-011** — Annotation format — one row per ground-truth event:

```csv
clip_id,t_start_ms,t_end_ms,event_type,severity,annotator,notes
c014,183400,185100,eye_closure,microsleep,NHT,"1.7s, subject 3, IR"
c014,201200,206800,yawn,full,NHT,"5.6s"
c022,ali,,eyes_off_road,phone,PVL,"glance sequence, see notes"
```

**TP-012** — Each corpus SHALL be split **by subject** into a tuning set and a held-out set. The
held-out set SHALL NOT be looked at during threshold tuning.
*Rationale: with a small team recording itself, tuning and evaluating on the same faces produces a
number that means nothing outside the room. Splitting by subject — not by clip — is what makes the
held-out result an honest estimate.*

**TP-013** — Ambiguous events (was that a yawn or a deep breath?) SHALL be annotated by two people
independently, and the disagreement rate SHALL be reported. **A detector cannot be more accurate
than its ground truth**, and if two humans agree only 80 % of the time on what a yawn is, a claimed
96 % yawn detection rate is not measuring what it says it measures.

**TP-014** — All corpus subjects SHALL give recorded consent. Corpus video SHALL NOT leave the
team's storage and SHALL NOT be included in any submission or public repository.

---

## 6. Acceptance criteria

The system is accepted for demonstration when **all** of the following hold on the held-out corpus
split, with a clean (non-`+dirty`) build.

| # | Criterion | Target | Traces to |
|---|---|---|---|
| AC-01 | Microsleep TPR (closures ≥ 1.5 s in C-DROWSY) | ≥ **0.95** | SYS-FR-011 |
| AC-02 | False alarms at L1+ on C-BASE | ≤ **1.0 / hour** | SYS-SR-008 |
| AC-03 | Yawn event detection F1 on C-DROWSY | ≥ **0.85** | DOM-D2-001 |
| AC-04 | Distraction (EOR ≥ 2 s) TPR on C-DROWSY | ≥ **0.90** | DOM-D1-002 |
| AC-05 | Pipeline latency, P95 | ≤ **200 ms** | SYS-PR-001 |
| AC-06 | Sustained FPS over 60 s | ≥ **8** | SYS-PR-003 |
| AC-07 | FPS at min 30 vs min 1 | ≥ **80 %** | SYS-PR-004 |
| AC-08 | Control-loop jitter | ≤ **±1 ms** | SYS-PR-006 |
| AC-09 | `LINK_LOST` entry after link break | 300 ms **+0/−50 ms** | CAN-061 |
| AC-10 | Safe stop after link break | 1000 ms +100/−0 ms | CAN-062 |
| AC-11 | Safe-stop duration, L3 to motors disabled | **2.0 s ± 0.1 s** | SYS-PR-007 |
| AC-12 | Zero unexpected states in a 30 min continuous run | 0 | SYS-SR-004 |
| AC-13 | Zero CAN bus-off events in a 30 min run | 0 | CAN-066 |
| AC-14 | All ⚠️ ASSUMPTION items closed | 0 open | README §5 |
| AC-15 | Sound pressure at 1 m | ≤ **85 dB(A)** | SYS-SR-007 |

**TP-020** — If any criterion is not met at the gate, the honest response is to **report the actual
number** in the demonstration, not to adjust the criterion. A team that says "our P95 latency is
240 ms, above our 200 ms target, and here is why" is more credible than one quoting a target as if
it were a measurement.

---

## 7. Entry and exit criteria

### Entry to L4 (system integration)
- [ ] Both nodes build clean from `main`
- [ ] CAN bring-up checklist ([04 §10](04-interface-control-document.md#10-bring-up-checklist)) complete
- [ ] CRC test vectors pass on both builds
- [ ] `tools/can_inject.py` can drive the VCS through all levels
- [ ] Chassis on a wheel stand

### Exit from L4 / entry to L5 (acceptance)
- [ ] All TC-CAN and TC-SAF cases pass
- [ ] 30-minute continuous run with no unexpected state and no bus-off
- [ ] All benchmark entries in [08](08-benchmark-log.md) filled with real measurements
- [ ] Open-items registers in specs 04 and 05 empty

### Exit from L5 (demo-ready)
- [ ] All acceptance criteria in §6 met, **or** each miss documented with its actual number
- [ ] Demo script rehearsed end to end twice with no operator intervention
- [ ] A recorded backup video of a successful run exists (the demo hardware may fail on the day)

---

## 8. Regression policy

**TP-030** — Every defect found at L3 or above SHALL result in a new test case at the **lowest**
level that could have caught it. A bug found on the bench that could have been caught by a corpus
replay indicates a missing L2 test, and that gap is the real defect.

**TP-031** — The L1 + L2 suite SHALL run on every PR and SHALL complete in under **3 minutes**. A
suite slower than that gets skipped under deadline pressure, which is exactly when it is needed.

**TP-032** — Threshold changes SHALL report the before/after on **both** AC-01 and AC-02 in the PR
description ([02 DEV-091](02-development-standards.md#11-change-control)). Sensitivity is always
purchasable with false alarms; the PR must show the price.

---

## 9. Roles

| Role | Responsibility |
|---|---|
| Test lead | Owns this plan, the corpora, and [08](08-benchmark-log.md); signs the acceptance gate |
| DMS-AP owner | L1/L2 suites for capture, inference, domains, fusion |
| Firmware owner (VCS) | L1/L3 for the MCU, control-loop timing, safe stop |
| Integration owner | L4 rig, CAN bring-up, logic-analyser captures |

**TP-040** — The person who wrote a module SHALL NOT be the only person who tests it at L4.
*Rationale: the author's mental model of "how you use it" is the same model that produced the bug.*

---

## Revision history

| Rev | Date | Author | Change |
|---|---|---|---|
| 0.1 | 2026-08-10 | ML_IoT_Love50 | Initial baseline |
