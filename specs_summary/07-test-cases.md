# Summary of 07 — Test Case Catalogue

Source: [specs/07-test-cases.md](../../specs/07-test-cases.md)

## Status
**130 test cases, 0 executed** (all `N/R` — not run) at Rev 0.1. Every case states a level
(L1-L5), the requirement it verifies, and a pass criterion **checkable without judgement** — if
two engineers could disagree on whether a case passed, the case is badly written and needs fixing.

## The 9 groups (not listing all 130 cases — just what each group checks and the notable ones)

| Group | Count | What it checks |
|---|---|---|
| **TC-ARC** | 6 | Architecture & privacy — e.g. disconnect the camera and still drive the VCS via CAN injection; kill DMS-AP and the VCS still reaches a safe stop; CAN traffic contains no image/landmark data |
| **TC-DOM** | 40 | Every D1/D2/D3 threshold — each has a "just below threshold → no trigger" and "at threshold → triggers" pair. Some use real corpus data to measure TPR/F1 (AC-01,03,04) |
| **TC-FUS** | 24 | The entire alert ladder — domain combinations, escalate/de-escalate, ack, hysteresis, and actuation matching the VEH-030 table |
| **TC-CAN** | 15 | CAN physical layer, CRC, encode/decode round-trip, timeouts, triple-ACK, emergency-stop magic byte |
| **TC-SAF** | 17 | **The group that decides demo-readiness** — safe power-on, link loss, safe-stop not auto-resuming, watchdog, motor stall, undervoltage, e-stop working even with the MCU halted in a debugger |
| **TC-VEH** | 15 | Motors: MIN_MOVE_DUTY, exact speed caps, ramp rate, safe-stop timing/deceleration, PWM frequency, jitter, stack headroom, sound level |
| **TC-PERF** | 12 | Latency P50/P95, FPS, thermal throttle, RAM/flash, ≤30s cold start, GPIO marker overhead |
| **TC-ROB** | 12 | Harsh environments — glare, dark+IR, glasses, face mask, subject leaving frame, two faces in frame, chassis vibration, brown-out, 3-hour soak |
| **TC-DEV** | 6 | Process — ICD regen has no drift, thresholds match code, boot banner correct, deploy matches SHA |

## The most memorable cases (illustrate "safety that's actually real, not just on paper")
- **TC-SAF-013**: press the physical e-stop **while the MCU is halted in a debugger** — motors
  must still stop. Proves emergency-stop doesn't depend on firmware.
- **TC-CAN-014**: inject a valid `EMERGENCY_STOP` at full speed → motors must disable within ≤1
  control cycle (10ms).
- **TC-DOM-037 / TC-VEH-023 (SENSOR_LOST)**: cover the lens for 5s → `SENSOR_LOST` at 3s, blue
  flashing light, **buzzer must be silent** — clearly distinguishes "camera obstructed" from "drowsy."
- **TC-SAF-009**: enter L3, then send L0 300ms into the ramp → the ramp must **run to
  completion**, no re-accelerating mid-manoeuvre.
- **TC-DOM-028/029**: a closure with one low-confidence frame in the middle → the accumulator
  must **hold**, not reset or increment; and the same 800ms event must be detected correctly
  whether running at 10FPS or 3FPS (dwell counted in real time, not frame count).
- **TC-FUS-016**: from L2, all domains clear for 5.0s → must step down to **L1 only**, never
  jump straight to L0.

## Traceability
Every requirement group (SYS-AR/FR/PR/IR/SR/ER, DOM-D1/D2/D3, DOM-FUS/FLT, CAN-*, VEH-*, DEV-*)
maps to a specific range of test cases. A `tools/check_traceability.py` script runs before the
acceptance gate to ensure **no requirement is left uncovered** by any test case.
