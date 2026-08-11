# Summary of 05 — Vehicle Control Specification (VCS · FRDM-MCXN947)

Source: [specs/05-vehicle-control-spec.md](../../specs/05-vehicle-control-spec.md)

## Role
Consume `alert_level` from CAN → translate into vehicle behaviour + physical alerts → fail safe
when anything goes wrong. This is a small-scale simulated vehicle, **not** connected to and not
to be represented as connected to any real vehicle system. The real deliverable is the *state
machine and failure behaviour* — the motors are just how you can see it working; a judge should
be able to unplug the CAN cable and watch it react correctly.

## Hardware
- 4× TT-type motors ~1:48, differential arrangement (left FL+RL, right FR+RR paralleled per channel).
- H-bridge driver: **preferred TB6612FNG** (native 3.3V logic, 20kHz PWM above the audible band,
  low voltage drop ~0.5V) vs. **fallback L298N** (needs 5V level-shifting, PWM only ~8kHz
  audible — the whine directly competes with the buzzer that's supposed to wake the driver).
- Pin mapping cross-checked against schematic + SDK reference example, **builds clean**, not yet flashed.
- Current sense not wired yet (`OI-05-01`) — firmware treats a 0 reading as "not wired," never as a fault.
- Must avoid PIO1_3 (connected to a touch electrode via R156).

## State machine (normative)
```
INIT → DISARMED → ARMED_IDLE → RUN ⇄ LIMITED → DECEL → STOPPED
                                              (any state) → ESTOP / FAULT
```
- Reset always enters `INIT`, motor disabled, duty=0.
- `DISARMED→ARMED_IDLE` requires **both** an operator arm input AND `flag_calib_done=1`.
- `STOPPED` only exits via explicit operator re-arm, never on alert level alone.
- `ESTOP` is enterable from **every** state within one control cycle, no ramp.
- Explicit `switch` over an enum, with a default → log + go to `FAULT`.

## Speed governing
```
duty_out = clamp(throttle_setpoint × speed_cap(level), 0, 100)
```
| Level | Speed cap | Rationale |
|---|---:|---|
| L0 | 100% | No intervention |
| L1 | 80% | Gentle, mostly-unnoticed reduction — shortens stopping distance without feeling punitive |
| L2 | 50% | Now visibly an intervention — halving speed roughly quarters kinetic energy |
| L3 | 0% (safe-stop) | Assume no driver |
| `LINK_LOST` | 30% | Degraded and unknown — more conservative than any known drowsiness state below L3 |

- Speed-cap transitions are **rate-limited** (≤40%/s), never stepped — avoids lurching/skidding.
- Motors don't turn below ~20% duty (static friction) → mapping uses `MIN_MOVE_DUTY=25` (must
  be re-measured on the real loaded chassis).
- Left/right channels commanded independently (allows a steering demo) but both share the same
  cap and ramp limiter.

## Alert actuation — the table worth memorizing
| Level | Buzzer | LED | Vibration | Fan | Hazard | Speed |
|---|---|---|---|---|---|---|
| L0 | silent | green | off | off | off | 100% |
| L1 | 2kHz, **2 pulses then stop** | amber | off | off | off | 80% |
| L2 | 2.8kHz continuous | red | on | on | off | 50% |
| L3 | 3.2/2.4kHz alternating continuous | red flash 4Hz | on | on | **alternating 1Hz** | safe-stop |
| SENSOR_LOST | **silent** | blue flash 1Hz | off | off | off | 80% |
| LINK_LOST | 1kHz brief/2s | amber flash 2Hz | off | off | off | 30% |
| FAULT/ESTOP | 1kHz continuous | red flash 1Hz | off | off | on | 0% |

- L1 is only **2 pulses then silence** while the level persists — a continuous tone at the
  earliest level is the fastest way to teach a driver to hate the device.
- `SENSOR_LOST` must be **silent** and visually distinct (blue) — a camera obstruction is not a
  drowsy driver, sounding an alarm for it is wrong and the fastest way to get the camera taped over.
- Audio capped ≤85dB(A)@1m — must be **measured**, not assumed from a datasheet.
- Actuator patterns run from a single non-blocking pattern engine at the 100Hz tick — no
  `delay()`, no busy-wait.
- Any level change must settle actuator state within ≤20ms of the decoded CAN frame.

## Safe-stop (normative)
| Phase | Duration | Action |
|---|---|---|
| 1 — Ramp | 1500ms | Linear duty reduction to 0, equal on both channels |
| 2 — Brake | 500ms | Active braking (short both H-bridge inputs) |
| 3 — Hold | indefinite | Motor disabled, duty=0, hazard 1Hz, state `STOPPED` |

Total: **2.0s** commanded-motion-to-standstill. Braking must be **symmetric** across both
channels (asymmetric braking on a differential platform causes a spin — exactly the wrong
behaviour to demonstrate). A safe-stop already in progress **cannot be aborted** by a lower
alert level arriving mid-ramp — it must run to completion (if the driver's eyes open 200ms into
the stop, the vehicle was still decelerating with a driver who was unconscious a moment ago;
completing the stop is correct and predictable, resuming acceleration mid-manoeuvre is not).

## Failsafe
- Follows the timeouts in spec 04 exactly (`LINK_LOST` 300ms, safe-stop 1000ms).
- Missing `DMS_STATUS` **never** equals L0.
- WWDT 500ms, serviced only from `control_task` at 100Hz after completing a full iteration
  (kicking it from a timer ISR proves the timer runs, not that the control loop is still making decisions).
- Watchdog-caused reset → enters `DISARMED`, sets `fault_watchdog_reset` for 5s, logs ERROR,
  **never silently resumes**.
- Motor current above `I_STALL_LIMIT` for >500ms → `fault_driver`, disable driver, enter `FAULT`.
- Motor rail below `V_UNDERVOLT` for >200ms → `fault_undervoltage`, enter `FAULT`.
- Every fault path ends in a state **no more permissive** than before — no code path from any
  fault state directly to `RUN`.

## Task structure (FreeRTOS)
| Task | Priority | Period | Responsibility |
|---|---|---|---|
| `control_task` | highest−1 | 10ms (100Hz) | state machine, ramps, duty output, watchdog service |
| `can_rx_task` | highest | event | decode, validate, refresh supervisor |
| `alert_task` | medium | 10ms | pattern engine for buzzer/LED/haptics |
| `telemetry_task` | low | 100ms | transmit `VCS_STATUS`, console |

- `control_task` jitter ≤±1ms. Duty outputs are written **only from `control_task`** — one
  writer means the actuator state is always explainable by one task; two writers means a race
  that reproduces once an hour.

## Power and wiring
- Logic and motor supplies separately fused; motor supply isolatable while logic stays powered for debugging.
- One star ground point; motor return current doesn't share a conductor with the CAN
  transceiver ground or ADC reference.
- Every inductive load (motors, relay, vibration motor) needs flyback protection — a missing
  diode resets the MCU and looks exactly like a firmware crash.
- Bulk capacitor ≥1000µF at the driver supply input — four motors starting simultaneously is the
  worst-case load step in the system.
- The physical e-stop cuts **motor supply** (not a logic signal), using a latching mushroom-head switch.

## Open items still unclosed
Measure actual motor stall current · measure `MIN_MOVE_DUTY` on loaded chassis · decide
TB6612FNG vs L298N · measure the 85dB(A) limit · characterize actual deceleration profile
(already closed: pin assignment doesn't conflict with anything).
