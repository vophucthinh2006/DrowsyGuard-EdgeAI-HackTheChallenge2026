# 05 — Vehicle Control Specification (VCS · FRDM-MCXN947)

**Document:** DG-SPEC-05 · Rev 0.1 · 2026-08-10 · DRAFT
**Applies to:** `vcs-mcxn947/`
**Node role:** consume `alert_level` from CAN, translate it into vehicle behaviour and physical
alerts, and fail safe when anything goes wrong.

---

## 1. Scope and a necessary disclaimer

The VCS drives a four-wheel scale platform. It **simulates** a vehicle's response to driver
drowsiness; it is not connected to and SHALL NOT be represented as being connected to any real
vehicle system.

The design intent is that the *state machine and the failure behaviour* are the deliverable — the
motors are how you can see them working. A judge should be able to unplug the CAN cable and watch
the vehicle react correctly, because that is the part that would matter in a real product.

---

## 2. Hardware configuration

### 2.1 Drivetrain

| Item | Specification | Status |
|---|---|---|
| Motors | 4 × TT-type DC gear motor, ~1:48, 3–6 V | ⚠️ ASM-02 — stall current to be measured |
| Arrangement | Differential drive: left pair (FL+RL) and right pair (FR+RR) paralleled per channel | 🟡 |
| Driver — preferred | TB6612FNG dual H-bridge, 3.3 V logic, ~1.2 A/ch continuous | ⬜ |
| Driver — baseline | L298N dual H-bridge (commonly bundled with these motors) | ⬜ |
| Motor supply | 6.0–7.4 V, ≥ 3 A, separately fused from logic | ⬜ |
| Current sense | Shunt on the motor rail into LPADC, for stall/overcurrent detection | ⬜ |

**VEH-001** — The driver stage SHALL be selected as follows and the choice recorded:

| | TB6612FNG (preferred) | L298N (fallback) |
|---|---|---|
| Logic level | 3.3 V native | 5 V logic — **requires level shifting from the 3.3 V MCU** |
| Voltage drop | ~0.5 V total | ~2.0 V total — motors see ~2 V less than the rail |
| PWM frequency | up to 100 kHz | practical ceiling ~20 kHz, efficiency degrades |
| Efficiency | MOSFET, cool | BJT Darlington, needs a heatsink |

*Rationale for stating both: the yellow-motor kits ship with an L298N, so that is what the team
most likely has on the bench on day one. It works, but it drops 2 V and cannot be driven above
audibility without loss — and the audible PWM whine competes directly with the buzzer that is
supposed to wake the driver. If a TB6612FNG can be sourced, source it.*

**VEH-002** — PWM frequency SHALL be **20 kHz** with a TB6612FNG (above the audible band) and
**8 kHz** with an L298N. If the L298N is used, the resulting audible whine SHALL be recorded as a
known deviation, since it degrades the L1 "soft beep" alert
([SYS-SR-007](01-system-requirements.md#7-safety-requirements)).

### 2.2 MCU peripheral allocation

| Function | Peripheral | Notes |
|---|---|---|
| Motor PWM ×2 channels | eFlexPWM or CTIMER match | Two independent duty channels |
| Motor direction ×2, enable | GPIO | Enable SHALL default low at reset |
| CAN | FlexCAN0, classical mode | See [04](04-interface-control-document.md) |
| Buzzer | CTIMER PWM (tone generation) | Frequency-agile for the alert patterns |
| Status LED (R/G/B) | GPIO, **active-LOW** on this board | Red PIO0_10, green PIO0_27, blue PIO1_2 |
| Hazard lamps | GPIO ×2 | 1 Hz alternating |
| Vibration motor | GPIO + low-side switch | Not driven directly from a GPIO pin |
| Fan relay | GPIO + opto/relay module | Flyback protection mandatory |
| ACK button | GPIO with pull-up + ≥20 ms debounce | |
| Operator re-arm | GPIO with pull-up | Deliberately a separate control from ACK |
| E-stop sense | GPIO | Sense only — the actual cut is in hardware |
| Motor current sense | LPADC | |
| Watchdog | WWDT | |
| Console | LPUART, 115200 8N1 | |

**VEH-003** — All peripheral clock gates and resets SHALL be explicitly enabled at init
(see [02 DEV-032](02-development-standards.md#41-platform-specific-rules-learned-the-hard-way)). A
peripheral with no clock reads back zeros and produces no error.

**VEH-004** — Pin assignment SHALL avoid PIO1_3, which is connected to the board's touch electrode
through R156, and SHALL be checked against `pin_mux.c` before wiring, not after.

---

## 3. State machine — **NORMATIVE**

```
        power on
           │
           ▼
      ┌─────────┐  self-test pass
      │  INIT   │──────────────────┐
      └─────────┘                  ▼
                            ┌────────────┐
        ┌───────────────────│  DISARMED  │◄──────── operator re-arm
        │  arm (operator +  └────────────┘             ▲   ▲
        │  calib_done=1)          │                    │   │
        ▼                         │                    │   │
  ┌────────────┐                  │                    │   │
  │ ARMED_IDLE │                  │                    │   │
  └─────┬──────┘                  │                    │   │
        │ throttle > 0            │                    │   │
        ▼                         │                    │   │
    ┌───────┐   L1/L2 or LINK_LOST│                    │   │
    │  RUN  │───────────┐         │                    │   │
    └───┬───┘           ▼         │                    │   │
        │         ┌──────────┐    │                    │   │
        │◄────────│ LIMITED  │    │                    │   │
        │ cleared └────┬─────┘    │                    │   │
        │              │ L3 or CAN_TIMEOUT             │   │
        │              ▼                               │   │
        │        ┌──────────┐                          │   │
        └───────▶│  DECEL   │                          │   │
                 └────┬─────┘                          │   │
                      ▼                                │   │
                 ┌──────────┐   operator re-arm        │   │
                 │ STOPPED  │──────────────────────────┘   │
                 └──────────┘                              │
                                                           │
   any state ──── e-stop ──▶ ┌────────┐ ── release + re-arm┘
                             │ ESTOP  │
                             └────────┘
   any state ── critical fault ──▶ ┌───────┐
                                   │ FAULT │──▶ DISARMED on clear
                                   └───────┘
```

**VEH-010** — Reset state SHALL be `INIT` with motor enable de-asserted and both duties at zero.

**VEH-011** — Transition `DISARMED → ARMED_IDLE` SHALL require **both** an operator arm input and
`flag_calib_done == 1` in a valid `DMS_STATUS`
([CAN-014](04-interface-control-document.md#3-0x100-dms_status--the-safety-relevant-message)).

**VEH-012** — `STOPPED` SHALL NOT be exited on alert level alone. Only an explicit operator re-arm
leaves it ([SYS-FR-033](01-system-requirements.md#44-vehicle-simulation)).

**VEH-013** — `ESTOP` SHALL be enterable from every state, in one control cycle, with no ramp.

**VEH-014** — The state machine SHALL be a single `switch` with a `default:` that logs the invalid
state and transitions to `FAULT` ([02 DEV-027](02-development-standards.md#4-coding-standards--firmware-c)).

---

## 4. Speed governing

**VEH-020** — The commanded duty SHALL be:

```
duty_out = clamp( throttle_setpoint × speed_cap(level) , 0 , 100 )
```

where `speed_cap` is a function of the received alert level only:

| Alert level | `speed_cap_pct` | Rationale |
|---|---:|---|
| L0 NORMAL | **100 %** | No intervention |
| L1 EARLY | **80 %** | A gentle, mostly-unnoticed reduction. Enough to shorten stopping distance; not enough to feel punitive, which is what keeps the driver from disabling the device |
| L2 DROWSY | **50 %** | Now visibly an intervention. Halving speed roughly quarters kinetic energy |
| L3 DANGER | **0 %** via safe stop | Assume no driver |
| `LINK_LOST` | **30 %** | Degraded and unknown — more conservative than any known drowsiness state below L3 |
| Fault | **0 %** | |

**VEH-021** — Speed-cap transitions SHALL be **rate-limited**, not stepped. Cap changes SHALL be
applied at a maximum of **40 % of full duty per second**.
*Rationale: an instantaneous 100 % → 50 % step on a light scale platform makes it lurch or skid,
which looks like a defect and is a genuinely bad behaviour to model. Real driver-assistance systems
ramp.*

**VEH-022** — TT gear motors do not turn below roughly 20 % duty (static friction). The output
mapping SHALL therefore be:

```
if setpoint == 0:  duty = 0
else:              duty = MIN_MOVE_DUTY + setpoint × (100 - MIN_MOVE_DUTY) / 100
```

with `MIN_MOVE_DUTY = 25`. This SHALL be re-measured on the actual chassis and recorded in
[08](08-benchmark-log.md), because it varies with load and battery state.

**VEH-023** — Left and right channel duties SHALL be commanded independently so a differential
(steering) demo is possible, but both SHALL be subject to the same cap and the same ramp limiter.

---

## 5. Alert actuation

**VEH-030** — Per-level actuation — **NORMATIVE**:

| Level | Buzzer | Status LED | Vibration | Fan relay | Hazard | Speed |
|---|---|---|---|---|---|---|
| **L0** | silent | green steady | off | off | off | 100 % |
| **L1** | 2 kHz, 100 ms on / 400 ms off, **2 pulses then stop** | amber steady | off | off | off | 80 % |
| **L2** | 2.8 kHz, 200 ms on / 200 ms off, continuous | red steady | on, 500 ms/1 s duty | on | off | 50 % |
| **L3** | 3.2 kHz + 2.4 kHz alternating, 150 ms, continuous | red, 4 Hz flash | on, continuous | on | **1 Hz alternating** | safe stop |
| `SENSOR_LOST` | **silent** | blue, 1 Hz flash | off | off | off | 80 % |
| `LINK_LOST` | 1 kHz, 50 ms every 2 s | amber, 2 Hz flash | off | off | off | 30 % |
| `FAULT` / `ESTOP` | 1 kHz continuous | red, 1 Hz flash | off | off | on | 0 % |

**VEH-031** — The L1 alert SHALL be **finite** (two pulses, then silence) while the level persists.
It repeats only on re-entry to L1.
*Rationale: a continuous tone at the earliest level is the fastest way to teach a driver to hate
the device. L1 informs. L2 insists.*

**VEH-032** — `SENSOR_LOST` SHALL be **silent and visually distinct** (blue, not amber or red). A
camera obstruction is not a drowsy driver, and sounding a drowsiness alarm for it is both wrong and
the fastest route to the device being covered with tape.

**VEH-033** — Audio output SHALL be limited so that it cannot exceed 85 dB(A) at 1 m
([SYS-SR-007](01-system-requirements.md#7-safety-requirements)). This SHALL be verified by
measurement and recorded, not assumed from the buzzer datasheet.

**VEH-034** — Actuator patterns SHALL be generated from a single non-blocking pattern engine driven
by the 100 Hz control tick. No `delay()`, no busy-wait, anywhere in the alert path
([02 DEV-025](02-development-standards.md#4-coding-standards--firmware-c)).

**VEH-035** — On any level change the actuator state SHALL settle within **20 ms** of the decoded
CAN frame, contributing to the end-to-end budget in
[01 §5](01-system-requirements.md#5-performance-requirements).

---

## 6. Safe stop

**VEH-040** — The safe-stop sequence — **NORMATIVE**:

| Phase | Duration | Action |
|---|---|---|
| 1 — Ramp | `T_RAMP` = **1500 ms** | Linear duty reduction from current value to 0, both channels equally |
| 2 — Brake | `T_BRAKE` = **500 ms** | Active braking (both H-bridge inputs asserted = motor short) |
| 3 — Hold | indefinite | Motor enable de-asserted, duty 0, hazard 1 Hz, state `STOPPED` |

Total commanded-motion-to-standstill: **2.0 s**, meeting
[SYS-PR-007](01-system-requirements.md#5-performance-requirements).

**VEH-041** — The ramp SHALL be **linear in duty**, and the resulting deceleration SHALL be
measured and reported. Duty is not speed; claiming a deceleration figure derived from the duty ramp
without measuring it would be a fabricated number.

**VEH-042** — Braking SHALL be **symmetric** on both channels. Asymmetric braking on a differential
platform causes a spin, which is exactly the wrong behaviour to demonstrate.

**VEH-043** — Once phase 3 is entered, the VCS SHALL enter `STOPPED` and SHALL ignore all
`alert_level` values until an operator re-arm (VEH-012).

**VEH-044** — A safe stop already in progress SHALL NOT be abortable by a lower alert level
arriving mid-ramp. It SHALL run to completion.
*Rationale: if the driver's eyes open 200 ms after the stop began, the vehicle is already
decelerating with a driver who was unconscious a moment ago. Completing the stop is the correct and
predictable behaviour; a vehicle that resumes acceleration mid-manoeuvre is not.*

---

## 7. Failsafe behaviour

**VEH-050** — CAN supervision SHALL be exactly as specified in
[04 §8](04-interface-control-document.md#8-timeout-supervision--normative):
`LINK_LOST` at 300 ms, safe stop at 1000 ms.

**VEH-051** — A missing `DMS_STATUS` SHALL NEVER be treated as L0
([CAN-063](04-interface-control-document.md#8-timeout-supervision--normative)).

**VEH-052** — A **WWDT** window watchdog SHALL be configured with a 500 ms period and SHALL be
serviced only from the 100 Hz control task, and only after that task has completed a full
iteration.
*Rationale: kicking the watchdog from a timer ISR proves the timer is running. It proves nothing
about whether the control loop is still making decisions — which is the thing that must not stop.*

**VEH-053** — On reset, the VCS SHALL determine the reset cause and, if it was the watchdog, SHALL
enter `DISARMED`, set `fault_watchdog_reset` for 5 s, and log an `ERROR`. It SHALL NOT silently
resume.

**VEH-054** — Motor current above `I_STALL_LIMIT` for more than 500 ms SHALL set `fault_driver`,
disable the driver stage and enter `FAULT`. `I_STALL_LIMIT` SHALL be set from a measurement of the
actual motors (ASM-02), not from a datasheet typical value.

**VEH-055** — Motor rail below `V_UNDERVOLT` for more than 200 ms SHALL set `fault_undervoltage`
and enter `FAULT`. A browning-out battery produces erratic motion that is easily mistaken for a
control bug and can wedge the MCU.

**VEH-056** — Every fault path SHALL end in a state no more permissive than the state it left
([SYS-SR-004](01-system-requirements.md#7-safety-requirements)). There SHALL be no transition in
the code from any fault state directly to `RUN`.

---

## 8. Timing and task structure

**VEH-060** — The VCS SHALL run FreeRTOS with the following tasks:

| Task | Priority | Period | Responsibility |
|---|---|---|---|
| `control_task` | highest − 1 | **10 ms (100 Hz)** | State machine, ramps, duty output, watchdog service |
| `can_rx_task` | highest | event | Decode, validate, refresh supervisor, post to control |
| `alert_task` | medium | 10 ms | Pattern engine for buzzer/LED/haptics |
| `telemetry_task` | low | 100 ms | `VCS_STATUS` transmit, console output |

**VEH-061** — `control_task` jitter SHALL be ≤ ±1 ms
([SYS-PR-006](01-system-requirements.md#5-performance-requirements)), measured by toggling a GPIO
at the top of each iteration and observing it on a scope for ≥ 60 s.

**VEH-062** — CAN reception SHALL post to `control_task` via a queue. Duty outputs SHALL be written
**only** from `control_task`.
*Rationale: one writer for the actuators means the actuator state can always be explained by one
task's state. Two writers means a race that reproduces once an hour.*

**VEH-063** — FreeRTOS on this SDK requires all three of these in `prj.conf`; setting only the port
option silently disables the port
([02 DEV-031](02-development-standards.md#41-platform-specific-rules-learned-the-hard-way)):

```
CONFIG_MCUX_COMPONENT_middleware.freertos-kernel=y
CONFIG_MCUX_COMPONENT_middleware.freertos-kernel.heap_4=y
CONFIG_MCUX_COMPONENT_middleware.freertos-kernel.cm33_non_trustzone=y
```

**VEH-064** — Task stacks SHALL be sized from measured high-water marks
(`uxTaskGetStackHighWaterMark`) with ≥ 50 % headroom, and the measurement SHALL be recorded in
[08](08-benchmark-log.md).

---

## 9. Power and wiring

**VEH-070** — Logic and motor supplies SHALL be separately fused. The motor supply SHALL be
isolatable while logic remains powered, so the state machine can be debugged with nothing spinning.

**VEH-071** — A single star ground point SHALL be used. Motor return current SHALL NOT share a
conductor with the CAN transceiver ground or the ADC reference.

**VEH-072** — Every inductive load (motors, relay coil, vibration motor) SHALL have flyback
protection at the load. A relay coil switched without a diode will reset the MCU, and the symptom
looks exactly like a firmware crash.

**VEH-073** — A bulk capacitor (≥ 1000 µF) SHALL be fitted at the motor driver supply input. Four
motors starting simultaneously is the worst-case load step in the system.

**VEH-074** — The physical e-stop SHALL interrupt the **motor supply**, not a logic signal, and
SHALL be a latching mushroom-head switch.

---

## 10. Open items

| ID | Item | Owner | Due |
|---|---|---|---|
| OI-05-01 ⚠️ | Measure actual TT motor stall current and set `I_STALL_LIMIT` | HW | Before first motion test |
| OI-05-02 ⚠️ | Measure `MIN_MOVE_DUTY` on the loaded chassis | HW | Before first motion test |
| OI-05-03 | Decide TB6612FNG vs L298N and record the deviation if L298N | HW | Day 1 |
| OI-05-04 | Verify 85 dB(A) limit by measurement | Test | Before demo |
| OI-05-05 | Confirm chosen pins do not collide with `pin_mux.c` defaults or PIO1_3 | FW | Before wiring |
| OI-05-06 | Characterise the actual deceleration profile from the 1500 ms duty ramp (VEH-041) | Test | Before demo |

---

## Revision history

| Rev | Date | Author | Change |
|---|---|---|---|
| 0.1 | 2026-08-10 | ML_IoT_Love50 | Initial baseline |
