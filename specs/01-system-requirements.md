# 01 — System Requirements Specification (SyRS)

**Document:** DG-SPEC-01 · Rev 0.1 · 2026-08-10 · DRAFT
**Applies to:** DrowsyGuard complete system (DMS + VCS + CAN link + demonstration rig)

---

## 0. Implementation status — what is actually running today (added 2026-08-16)

This document specifies the full target system: a MediaPipe face-landmark pipeline
feeding a three-domain (D1/D2/D3) fusion engine on the DMS-AP, and a VCS with vibration
motor, fan and hazard-lamp actuation, gated behind an explicit operator re-arm button.
**That is not what is deployed on real hardware today.** Two things have shipped
instead, independently, and neither matches this document exactly:

- **DMS side** — `qualcomm_AI/MAIN_DMS_YOLOX_System/copy-of-object-hunting/` is the only
  camera-driven implementation running on real hardware. It uses an Edge Impulse YOLOX
  Nano model (3 detection classes: `closed_eye`/`open_eye`/`yawning`, no landmarks, no
  head pose) instead of MediaPipe, has no D1 (distraction) domain, and has no fusion
  ladder — see [03 §0](03-drowsiness-domain-spec.md#0-implementation-status--what-is-actually-running-today-added-2026-08-16)
  for the full breakdown. `qualcomm_AI/dms-ap-uno-q/` separately implements the *formal*
  CAN/ICD/Bridge path this document assumes, correctly, but with a keyboard (`0`-`3`)
  standing in for the vision pipeline — no camera at all. These two DMS implementations
  have not been reconciled into one.
- **VCS side** — `qualcomm_AI/vcs-mcxn947/` had its hardware scope trimmed on 2026-08-15
  to match its system block diagram: the vibration motor, fan relay, hazard lamps, ACK
  button, and operator re-arm button in §2's diagram and in SYS-FR-032/033 below **do
  not exist in the built hardware**. Re-arm out of `STOPPED` is now automatic (1 s
  continuously at L0 + CAN link OK), not an explicit operator input — a direct deviation
  from SYS-FR-033, acknowledged as deliberate in `vcs-mcxn947/README.md` and
  `PINOUT.md`, not an oversight. See [05 §0](05-vehicle-control-spec.md#0-implementation-status--what-is-actually-running-today-added-2026-08-16).

Every `SYS-*` requirement below is left as originally written — this section exists so
the gap is documented once, at the top, rather than discovered requirement-by-requirement.

---

## 1. Purpose and scope

### 1.1 Purpose

This document defines the complete set of requirements the DrowsyGuard demonstration system
SHALL satisfy. It is the top-level contract; all lower-level specifications
([03](03-drowsiness-domain-spec.md), [04](04-interface-control-document.md),
[05](05-vehicle-control-spec.md)) SHALL trace upward to a requirement stated here, and every
requirement here SHALL be covered by at least one test case in
[07](07-test-cases.md).

### 1.2 Scope — in

- Real-time driver-state estimation from a single camera, performed entirely on-device.
- Fusion of three independent evidence domains into a four-level alert ladder.
- A deterministic CAN link carrying the driver state to a vehicle controller.
- A physical scale-vehicle simulator (4-motor differential drive) that reacts to the alert level
  by limiting speed and, at the top level, performing a controlled safe stop.
- Escalating physical driver alerts (audible, visual, haptic).
- The measurement rig and procedures used to prove all of the above.

### 1.3 Scope — out

The following are explicitly **not** in scope for the competition build and SHALL NOT be
implied by any demonstration:

- Any interface to a real vehicle powertrain, brake system or CAN network.
- Any claim of automotive functional-safety compliance (ISO 26262 / ASIL). The architecture
  *borrows* practices from it; it is not certified to it.
- Cloud connectivity, fleet dashboard, or per-driver profile persistence beyond a single session.
- Driver identification or biometric enrolment.

> **Statement required at every demonstration:** DrowsyGuard is a research demonstrator. The
> scale vehicle is a simulator. No part of this system has been qualified for use in a road
> vehicle.

### 1.4 Definitions

| Term | Definition |
|---|---|
| **DMS** | Driver Monitoring System — the Arduino UNO Q node |
| **DMS-AP** | Application processor of the DMS — QRB2210, Linux, runs vision + fusion |
| **DMS-RT** | Real-time MCU of the DMS — STM32U585, runs alert actuation + CAN |
| **VCS** | Vehicle Control Simulator — the FRDM-MCXN947 node + chassis |
| **Domain** | One independent stream of drowsiness evidence (D1/D2/D3) — see [03](03-drowsiness-domain-spec.md) |
| **Alert level** | The fused system output, L0…L3 |
| **PERCLOS** | Percentage of eyelid closure over a rolling time window (P80 definition) |
| **Dwell time** | How long a condition must hold continuously before it is declared |
| **Pipeline latency** | Time from photon capture to actuator change, *excluding* dwell time |
| **Safe stop** | Commanded controlled deceleration of the simulator to standstill |
| **Ack** | The driver's "I am awake" acknowledgement input |

---

## 2. System context

> Target architecture — see [§0](#0-implementation-status--what-is-actually-running-today-added-2026-08-16) for what's actually built (no MediaPipe, no vibration motor/fan/hazard lamps).

```
                    ┌──────────────────────────────────────────────┐
                    │              DRIVER (human)                  │
                    └───▲────────────────▲───────────────┬─────────┘
        face, eyes,     │                │ sees / hears  │ presses
        mouth, head pose│                │ / feels       │ "I am awake"
    ┌───────────────────┴────┐           │               │
    │  Camera (RGB + IR)     │           │               │
    └───────────┬────────────┘           │               │
                │ MIPI/USB               │               │
   ┌────────────▼───────────────────────────────────┐    │
   │ DMS — Arduino UNO Q                            │    │
   │ ┌────────────────────┐   ┌───────────────────┐ │    │
   │ │ DMS-AP  QRB2210    │   │ DMS-RT  STM32U585 │ │◄───┘
   │ │ Linux              │──▶│ real-time         │ │
   │ │ MediaPipe face-landmark pipeline │   │ alert actuation   │ │
   │ │ D1/D2/D3 + fusion  │   │ FDCAN             │ │
   │ └────────────────────┘   └─────────┬─────────┘ │
   └──────────────────────────────────────┬─────────┘
                                          │ CAN 500 kbit/s
                                          │ (2-node, 120 Ω both ends)
   ┌──────────────────────────────────────▼─────────┐
   │ VCS — FRDM-MCXN947                             │
   │  FlexCAN · motion control · failsafe · watchdog │
   └───┬─────────────────────┬──────────────────────┘
       │ PWM + DIR           │ GPIO
   ┌───▼───────────┐   ┌─────▼──────────────────────┐
   │ H-bridge      │   │ Buzzer · LEDs · vibration  │
   │ 4× TT motor   │   │ motor · hazard lamps       │
   └───────────────┘   └────────────────────────────┘
```

### 2.1 Actors

| Actor | Interaction |
|---|---|
| Driver | Observed by camera; receives alerts; may acknowledge |
| Operator / demonstrator | Powers up, starts run, injects test stimuli |
| Test engineer | Attaches logic analyser / scope probes, collects logs |

---

## 3. System architecture requirements

**SYS-AR-001** — The system SHALL consist of exactly two intelligent nodes (DMS, VCS) connected
by a single CAN segment.

**SYS-AR-002** — The DMS SHALL perform all image acquisition, inference and drowsiness fusion.
The VCS SHALL perform no image processing and SHALL NOT require any knowledge of how the alert
level was derived.
*Rationale: the level is the only coupling point. This lets the vision pipeline be replaced or
retuned without re-validating vehicle behaviour, and lets the vehicle behaviour be tested with a
CAN frame injector and no camera at all.*

**SYS-AR-003** — The safety-critical actuation path (CAN receive → speed limit → safe stop) SHALL
execute entirely on the VCS MCU, with no dependency on the Linux side remaining responsive.

**SYS-AR-004** — No image, video frame, facial landmark set, or any derived biometric SHALL be
transmitted off the DMS node, over CAN or otherwise. Only the aggregate metrics defined in
[04](04-interface-control-document.md) leave the node.

**SYS-AR-005** — The system SHALL be operable with no network connection of any kind. Absence of
Wi-Fi, cellular or Internet SHALL NOT change any detection or actuation behaviour.

**SYS-AR-006** — All persistent state SHALL be discarded at power-off except firmware,
model weights and the threshold configuration file. No driver data SHALL survive a power cycle.

---

## 4. Functional requirements

### 4.1 Perception

**SYS-FR-001** — The DMS SHALL capture frames from a driver-facing camera at a configured
capture rate of **≥ 10 frames per second**.

**SYS-FR-002** — The DMS SHALL run a face-landmark detector producing, per frame, a consistent face landmark set and a head-pose estimate (yaw, pitch). From the landmarks, the pipeline SHALL derive eye-open/closed, mouth-open, and face-presence states.
*Not implemented as written: the deployed model (`MAIN_DMS_YOLOX_System`) is a YOLOX Nano object detector outputting `closed_eye`/`open_eye`/`yawning` class labels directly, with no landmark set and no head-pose estimate — see [§0](#0-implementation-status--what-is-actually-running-today-added-2026-08-16).*

**SYS-FR-003** — Each detection SHALL carry a confidence score in [0.0, 1.0]. Detections below
the configured confidence floor SHALL be treated as absent, not as negative evidence.
*Rationale: "I did not see an open eye" and "I saw a closed eye" are different facts. Conflating
them turns a camera obstruction into a drowsiness alarm.*

**SYS-FR-004** — The DMS SHALL operate in both daylight (RGB) and dark-cabin (IR illumination)
conditions, and SHALL indicate which mode is active in the `night_mode` status flag.

**SYS-FR-005** — When no face is detected for longer than the sensor-loss dwell
(see [03 §7](03-drowsiness-domain-spec.md#7-fault-and-degraded-states)), the DMS SHALL enter
`SENSOR_LOST` and SHALL signal it as a **fault**, distinguishable from every drowsiness state,
on both the CAN bus and the local indicator.

### 4.2 Drowsiness estimation

**SYS-FR-010** — The DMS SHALL maintain three independent detection domains:
**D1 Distraction**, **D2 Yawning**, **D3 Eye closure**. Full definitions, thresholds and dwell
times are normative in [03](03-drowsiness-domain-spec.md).
*Not implemented as written: the deployed system runs D2- and D3-equivalent detection only; D1
(distraction) is absent because the model has no head-pose output — see
[03 §0](03-drowsiness-domain-spec.md#0-implementation-status--what-is-actually-running-today-added-2026-08-16).*

**SYS-FR-011** — Each domain SHALL produce a discrete state, and the domain states SHALL be fused
into a single alert level **L0 NORMAL / L1 EARLY / L2 DROWSY / L3 DANGER**.

**SYS-FR-012** — No alert level above L0 SHALL be raised on the evidence of a single frame. Every
transition SHALL require its specified dwell time to elapse with the condition continuously true.

**SYS-FR-013** — Level de-escalation SHALL be hysteretic: the clearing condition SHALL be
strictly weaker in time than the setting condition (see [03 §6](03-drowsiness-domain-spec.md#6-fusion-and-the-alert-ladder)).

**SYS-FR-014** — The system SHALL provide a driver acknowledgement ("I am awake") input that
clears L1 and L2 and starts a refractory period.

**SYS-FR-015** — Acknowledgement SHALL NOT be capable of suppressing the eye-closure critical
condition (D3 CRITICAL).
*Rationale: a driver who is genuinely asleep cannot press a button. An ack path that can mask the
one signal that indicates unconsciousness is a defeat device, not a feature.*

### 4.3 Alerting

**SYS-FR-020** — The system SHALL escalate alerts monotonically through L0 → L1 → L2 → L3, and
SHALL NOT skip a level *except* on entry to L3 by the D3 CRITICAL path, which MAY be entered
directly from any level.

**SYS-FR-021** — Per-level actuation SHALL be as specified in
[05 §5](05-vehicle-control-spec.md#5-alert-actuation).

**SYS-FR-022** — A three-colour status indicator SHALL show the current level at all times
(green = L0, amber = L1, red = L2/L3, red flashing = fault).

### 4.4 Vehicle simulation

**SYS-FR-030** — The VCS SHALL drive four DC gear motors as a two-channel differential drive
(left pair, right pair).

**SYS-FR-031** — The VCS SHALL apply a speed cap that is a function of the received alert level,
per [05 §4](05-vehicle-control-spec.md#4-speed-governing).

**SYS-FR-032** — On L3, the VCS SHALL execute a **safe stop**: a controlled deceleration ramp to
zero, followed by active braking, followed by motor disable with hazard indication active.

**SYS-FR-033** — Once a safe stop has completed, the VCS SHALL remain in `STOPPED` and SHALL NOT
resume motion on alert level alone. Resumption SHALL require an explicit operator re-arm input.
*Rationale: automatically resuming after an unconsciousness event would be actively dangerous
behaviour to demonstrate, regardless of the fact that this is a scale model.*
**⚠️ Deviated from in the deployed VCS**: `vcs-mcxn947` has no operator re-arm button (removed
2026-08-15). `STOPPED`/`FAULT`/`ESTOP` now clear automatically once the alert level has held at
L0 with a valid CAN link for 1 continuous second (`safety.c`'s `SafeConditionsSustained()`). This
is exactly the failure mode this requirement's rationale warns against and is flagged here as an
open safety item, not a documentation gap to silently accept — see
[05 §0](05-vehicle-control-spec.md#0-implementation-status--what-is-actually-running-today-added-2026-08-16).

### 4.5 Diagnostics and logging

**SYS-FR-040** — Both nodes SHALL emit a boot banner containing firmware semantic version, git
short SHA, and build timestamp.

**SYS-FR-041** — The DMS SHALL log every level transition with a monotonic timestamp, the domain
states that caused it, and the metric values at that instant.

**SYS-FR-042** — Logs SHALL contain no image data and no personally identifying information.

**SYS-FR-043** — The DMS SHALL publish live pipeline metrics (achieved FPS, inference time,
dropped frames) at ≥ 2 Hz for test instrumentation.

---

## 5. Performance requirements

All timing budgets below are **exclusive of domain dwell time**. Dwell is a deliberate detection
delay defined in [03](03-drowsiness-domain-spec.md); pipeline latency is unwanted delay on top of
it. Test cases SHALL measure and report them separately.

**SYS-PR-001** — **End-to-end pipeline latency** from frame exposure to first actuator state
change SHALL be ≤ **200 ms** at the 95th percentile.

Allocated budget:

| Stage | Budget | Owner |
|---|---:|---|
| Sensor exposure + transfer + colour convert | 25 ms | DMS-AP |
| Pre-process (letterbox, normalise, quantise) | 10 ms | DMS-AP |
| Inference — MediaPipe face-landmark detector @ 320×320 (target; deployed model is YOLOX Nano, see §0) | 80 ms | DMS-AP |
| Post-process + domain update + fusion | 10 ms | DMS-AP |
| AP → RT handoff | 10 ms | DMS |
| CAN frame assembly + arbitration + transmission | 5 ms | DMS-RT |
| VCS receive → actuator output change | 20 ms | VCS |
| **Sum of budgets** | **160 ms** | |
| Margin to requirement | 40 ms | |

**SYS-PR-002** — **Frame-rate quantisation** SHALL be accounted for: at a capture rate of *f*,
declaring a dwell condition may be late by up to 1/*f*. At the required 10 FPS this is 100 ms and
SHALL be reported as part of every latency measurement, not hidden inside it.

**SYS-PR-003** — Sustained inference rate SHALL be ≥ **8 FPS** averaged over any 60-second window,
with a target of 10 FPS.
*Rationale: PERCLOS is a 60 s window statistic; its accuracy depends on sample count, not on
sample rate being high. 8 FPS gives ≥ 480 samples per window — ample. The frame rate floor exists
for the microsleep dwell, not for PERCLOS.*

**SYS-PR-004** — Over a 30-minute continuous run, the achieved FPS at minute 30 SHALL be ≥ 80 % of
the FPS at minute 1 (thermal-throttle limit).

**SYS-PR-005** — CAN bus load SHALL be ≤ 5 % at 500 kbit/s under all specified message cycles.

**SYS-PR-006** — The VCS control loop SHALL execute at 100 Hz with jitter ≤ ±1 ms.

**SYS-PR-007** — Safe-stop deceleration SHALL complete within **2.0 s ± 0.1 s** from L3 command
receipt to zero commanded duty.

**SYS-PR-008** — DMS-AP memory footprint SHALL be ≤ 512 MB RSS; VCS firmware SHALL leave
≥ 20 % of RAM and ≥ 20 % of flash free at link time.

---

## 6. Interface requirements

**SYS-IR-001** — The DMS ↔ VCS interface SHALL be **classical CAN 2.0A**, 11-bit identifiers,
**500 kbit/s**, sample point 87.5 %.
*Rationale: CAN is the interface a vehicle actually uses, which makes the demonstrator credible;
it is differential and therefore robust to the motor-driver noise that would corrupt a UART on the
same chassis; and its arbitration gives the emergency message deterministic priority over status
traffic. The cost is an external transceiver at each end — accepted.*

**SYS-IR-002** — The bus SHALL be terminated with 120 Ω at each physical end and at no other point.

**SYS-IR-003** — Message identifiers, byte layouts, cycle times and validity rules SHALL be exactly
as specified in [04](04-interface-control-document.md). No node SHALL transmit an identifier not
listed there.

**SYS-IR-004** — Every periodic frame SHALL carry a 4-bit rolling sequence counter and an 8-bit
CRC over its payload. A receiver SHALL discard any frame failing either check.

**SYS-IR-005** — Loss of the CAN link SHALL be detected within **300 ms** by the receiving node
and SHALL drive the failsafe behaviour in [05 §7](05-vehicle-control-spec.md#7-failsafe-behaviour).

**SYS-IR-006** — Both nodes SHALL operate at 3.3 V logic. No 5 V signal SHALL be connected to
either MCU pin without level translation.

---

## 7. Safety requirements

These requirements are written in the *style* of a safety case. They do not constitute
certification (see §1.3).

**SYS-SR-001** — The VCS SHALL power up with motors disabled and SHALL require an explicit arm
command before any motion is possible.

**SYS-SR-002** — Absence of valid CAN input SHALL be treated as a fault, never as "driver is
alert". The failsafe direction SHALL always be toward reduced speed.

**SYS-SR-003** — A hardware watchdog on the VCS SHALL reset the node if the control loop stops
servicing it, and the reset state SHALL be motors-disabled.

**SYS-SR-004** — Any single detected fault (CAN timeout, watchdog reset, driver-stage fault,
sensor loss) SHALL result in a state that is no more permissive than the state before the fault.

**SYS-SR-005** — The system SHALL provide a physical emergency-stop input that de-energises the
motor driver independently of firmware state.
*Rationale: every failsafe above is implemented in software running on a device that has never
been formally verified. A demonstrator with spinning wheels needs one stop path that does not
depend on any of it.*

**SYS-SR-006** — Motor supply and logic supply SHALL be separately fused, and the motor supply
SHALL be capable of being isolated while logic remains powered for debugging.

**SYS-SR-007** — The alert audio level SHALL be limited such that it cannot exceed 85 dB(A) at
1 m, and the L1 alert SHALL be a soft tone chosen not to startle.
*Rationale: an alert that makes a drowsy driver flinch has caused the incident it was meant to
prevent.*

**SYS-SR-008** — False-positive alarms SHALL be bounded: the system SHALL raise no more than
**1 alert per hour** at L1 or above when observing an alert, awake driver (measured against the
baseline corpus, [06 §5](06-test-plan.md#5-test-corpora)).

---

## 8. Environmental and operational requirements

**SYS-ER-001** — Operating temperature: 0 °C to +45 °C ambient (demonstrator; not automotive
grade).

**SYS-ER-002** — Illumination: the system SHALL function from 5 lux (IR-illuminated dark cabin) to
50 000 lux (direct sunlight through a windscreen), with performance targets stated separately per
condition in [06](06-test-plan.md).

**SYS-ER-003** — The system SHALL function with the driver wearing clear prescription glasses.
Behaviour with dark sunglasses SHALL be **explicitly degraded and declared**: D3 becomes
unavailable, the system SHALL report `model_degraded`, and SHALL NOT silently continue as if eye
state were being measured.

**SYS-ER-004** — Power: logic 5 V ≥ 2 A (USB-C or bench supply); motor rail 6–7.4 V ≥ 3 A.
Nominal total draw SHALL be characterised and recorded in [08](08-benchmark-log.md).

**SYS-ER-005** — Cold start: from power-on to first valid alert level published on CAN SHALL be
≤ 30 s. Until then the DMS SHALL publish `level = L0` with `calib_done = 0`, and the VCS SHALL
remain disarmed.

---

## 9. Verification matrix

Every requirement SHALL be verified by one of: **T** = test, **A** = analysis, **I** = inspection,
**D** = demonstration.

| Requirement group | Method | Test cases |
|---|---|---|
| SYS-AR-001…006 | I, A | TC-ARC-001…006 |
| SYS-FR-001…005 | T | TC-DOM-001…010 |
| SYS-FR-010…015 | T | TC-DOM-011…040, TC-FUS-001…020 |
| SYS-FR-020…022 | T, D | TC-FUS-021…030 |
| SYS-FR-030…033 | T | TC-VEH-001…020 |
| SYS-FR-040…043 | I, T | TC-DEV-001…005 |
| SYS-PR-001…008 | T | TC-PERF-001…015 |
| SYS-IR-001…006 | T, I | TC-CAN-001…015 |
| SYS-SR-001…008 | T, A | TC-SAF-001…015 |
| SYS-ER-001…005 | T | TC-ROB-001…012 |

Full catalogue: [07 — Test Case Catalogue](07-test-cases.md).

---

## 10. Constraints and assumptions

| ID | Statement | Status |
|---|---|---|
| CON-01 | Hardware (UNO Q) is on loan and arrives after this spec was written; all figures here are budgets | Accepted |
| CON-02 | Timeline to demonstration is 2026-08-16 — six days from Rev 0.1 | Accepted |
| CON-03 | QRB2210 has no large dedicated NPU; inference is CPU/GPU-bound, hence INT8 nano backbone | Accepted |
| ASM-01 | The STM32U585 on the UNO Q exposes an FDCAN peripheral on pins reachable from the headers | ✅ **CONFIRMED 2026-08-15** — FDCAN1 on D4(PA12)=TX/D5(PA11)=RX, bidirectional real-hardware link proven working; see `dms-ap-uno-q/README.md` |
| ASM-02 | The 4 TT motors are the common 1:48 yellow gear motors, ~3–6 V, ≈1.2 A stall | ⚠️ To be confirmed by measurement |
| ASM-03 | Literature-derived thresholds in [03](03-drowsiness-domain-spec.md) transfer acceptably to the team's camera geometry | ⚠️ Must be re-tuned on own corpus; deployed system currently ships un-retuned, different-from-literature values, see [03 §0](03-drowsiness-domain-spec.md#0-implementation-status--what-is-actually-running-today-added-2026-08-16) |

**ASM-01 was the single highest-risk open item in the system and is now resolved**: FDCAN1 is
reachable and confirmed bidirectional on real hardware (`dms-ap-uno-q/README.md`), using Arduino's
own first-party `CAN.h` wrapper (`arduino::ZephyrCAN`), not a raw Zephyr driver call and not the
SPI CAN controller fallback this section originally planned for.

---

## Revision history

| Rev | Date | Author | Change |
|---|---|---|---|
| 0.1 | 2026-08-10 | ML_IoT_Love50 | Initial baseline, pre-hardware |
| 0.1.1 | 2026-08-16 | ML_IoT_Love50 | Added §0 Implementation status; resolved ASM-01 to CONFIRMED; flagged SYS-FR-002/010/033 and the §5 latency table as deviated-from by the deployed system. No `SYS-*` requirement text changed. |
