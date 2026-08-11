# Summary of 01 — System Requirements Specification

Source: [specs/01-system-requirements.md](../../specs/01-system-requirements.md)

## Scope
- **In scope:** real-time on-device driver-state estimation from a single camera; fusion of 3
  domains → 4 alert levels; CAN link to the VCS; a 4-motor simulated vehicle that reacts to the
  alert level; escalating physical alerts (audible/visual/haptic); the measurement rig.
- **Out of scope:** no interface to a real vehicle powertrain/CAN, no claim of ISO 26262/ASIL
  compliance, no cloud/fleet dashboard, no driver identification.
- Mandatory statement at every demo: *"this is a research demonstrator, the vehicle is a
  simulator, nothing here is qualified for a real road vehicle."*

## Architecture (SYS-AR)
- Exactly 2 intelligent nodes (DMS, VCS) joined by one CAN segment.
- The DMS does all vision/inference/fusion; the VCS does **no** image processing and doesn't
  need to know how the alert level was derived — the level is the only coupling point.
- The safety-critical path (CAN receive → speed limit → safe-stop) runs entirely on the VCS
  MCU, independent of whether Linux is responsive.
- No image/video/landmark data ever leaves the DMS — only aggregate metrics over CAN.
- The system must work **fully offline** (no Wi-Fi/cellular required).
- All per-driver state is discarded at power-off.

## Functional requirements (SYS-FR)
- Camera ≥10 FPS; face-landmark detection + head pose; derives eye/mouth/face-presence states.
- Every detection carries a confidence score; below the floor → treated as "not seen," not as
  negative evidence.
- Works in both daylight (RGB) and dark-cabin (IR) — flagged by `night_mode`.
- Prolonged no-face → `SENSOR_LOST`, signalled as a **fault**, clearly distinct from any
  drowsiness alarm.
- 3 domains D1/D2/D3 → fusion → L0-L3. No level above L0 may be raised on a single frame — every
  transition requires its dwell time.
- De-escalation is hysteretic (the clearing condition must be weaker in time than the setting condition).
- A driver acknowledgement ("I am awake") input clears L1/L2; it **cannot** clear D3 CRITICAL
  (avoids being a defeat device).
- Escalation is monotonic L0→L1→L2→L3, except D3 CRITICAL may jump directly to L3 from anywhere.
- A three-colour indicator always shows the current level (green/amber/red/red-flashing = fault).
- The VCS drives a two-channel differential drive, caps speed by alert level, and executes a
  ramped safe-stop at L3.
- After a safe-stop the vehicle stays `STOPPED` and **never auto-resumes** — requires explicit
  operator re-arm.
- Both nodes print a boot banner (version, git SHA, timestamp); every level transition is
  logged; logs contain no images/PII.

## Performance requirements (SYS-PR) — numbers to remember
| Item | Threshold |
|---|---|
| End-to-end latency (P95) | ≤ **200 ms** (internal budget 160ms, 40ms margin) |
| Frame-rate quantisation | reported separately, never hidden inside latency |
| Sustained inference rate | ≥ **8 FPS** (target 10) over any 60s window |
| Thermal throttle over 30 min | FPS at min 30 ≥ 80% of FPS at min 1 |
| CAN bus load | ≤ 5% at 500 kbit/s |
| VCS control loop | 100 Hz, jitter ≤ ±1 ms |
| Safe-stop deceleration | 2.0s ± 0.1s |
| DMS-AP memory | ≤ 512 MB RSS; VCS keeps ≥20% RAM & flash free |

Latency budget breakdown (total 160ms, 40ms margin): capture+convert 25ms, pre-process 10ms,
**inference 80ms** (the largest chunk), post-process+fusion 10ms, AP→RT handoff 10ms, CAN 5ms,
VCS actuate 20ms.

## Interface & Safety (SYS-IR / SYS-SR)
- CAN 2.0A, 11-bit IDs, 500 kbit/s, sample point 87.5%, 120Ω termination at both ends.
- Every periodic frame carries a 4-bit sequence counter + CRC8; failing checks → discarded.
- Loss of the CAN link must be detected within **300 ms**.
- Both nodes run 3.3V logic; no 5V signal may be connected directly.
- The VCS always powers up disarmed, requiring an explicit arm command.
- Missing CAN input = a fault, never "the driver is alert." Failsafe direction is always toward
  reduced speed.
- A hardware watchdog on the VCS.
- A **physical emergency-stop button** cuts the motor driver directly, independent of firmware.
- Motor and logic supplies are separately fused.
- Alert volume capped at ≤85 dB(A) @1m; L1 must be a soft tone that doesn't startle the driver.
- **False-positive rate is hard-capped: ≤ 1 alert/hour at L1+ for an alert, awake driver** — a
  hard requirement, not an aspiration.

## Environmental requirements
- Operating temperature 0–45°C.
- Illumination from 5 lux (IR-illuminated dark cabin) to 50,000 lux (direct sunlight).
- Works with clear prescription glasses; **dark sunglasses make D3 explicitly UNAVAILABLE**
  (`model_degraded`), never silently continuing as if eyes were still measurable.
- Cold start to first published alert level: ≤ 30s.

## Biggest risks/assumptions
- **ASM-01 (⚠️ unconfirmed):** whether the STM32U585 on the UNO Q exposes FDCAN on
  header-reachable pins. This is the **single highest-risk item** in the system. If false → fall
  back to an SPI CAN controller (MCP2515-class), ~1 day of bring-up, decision within 24h of
  hardware arrival.
- ASM-02: actual stall current of the 4 TT motors not yet measured.
- ASM-03: thresholds are literature-derived, not yet tuned on the team's own corpus.
