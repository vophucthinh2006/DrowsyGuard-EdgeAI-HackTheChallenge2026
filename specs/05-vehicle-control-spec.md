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
| Driver | BTS7960 43 A half-bridge module, 1 per channel (2 total), 5 V-tolerant logic inputs | ⬜ |
| Motor supply | 6.0–7.4 V, ≥ 3 A, separately fused from logic | ⬜ |
| Current sense | `R_IS`/`L_IS` sense outputs on each module, into LPADC, for stall/overcurrent detection | ⬜ |

**VEH-001** — The driver stage SHALL be **BTS7960** (one module per channel, two total). Each
module exposes `RPWM`/`LPWM` (forward/reverse PWM inputs) and `R_EN`/`L_EN` (half-bridge enables)
rather than the single-PWM-plus-direction-pins interface of a conventional dual H-bridge IC:

| | BTS7960 |
|---|---|
| Logic level | 3.3–5 V compatible input thresholds — drives cleanly from the 3.3 V MCU, no level shifting |
| Voltage drop | low — MOSFET half-bridges, ~tens of mΩ Rds(on) per leg |
| PWM frequency | datasheet-rated up to ~25 kHz |
| Efficiency | MOSFET, no heatsink required at this current level |
| Continuous current | up to 43 A per module (motor stall current, ASM-02, is far below this) |

*Rationale: BTS7960 modules are the commonly-sourced alternative to the L298N/TB6612FNG for this
class of hobby chassis, and their headroom eliminates the L298N's voltage-drop and audible-whine
problems without needing to source a TB6612FNG specifically. The tradeoff is wiring: each channel
needs 2 PWM-capable MCU pins (`RPWM`+`LPWM`) instead of 1 PWM + 2 GPIO, so 2 motor channels need 4
PWM outputs, not 2 — see §2.2.*

**VEH-002** — PWM frequency SHALL be **20 kHz** (below the BTS7960's ~25 kHz ceiling and above the
audible band, so the PWM edge does not compete with the buzzer's L1 "soft beep" alert,
[SYS-SR-007](01-system-requirements.md#7-safety-requirements)).

**VEH-001a** — Drive convention for each BTS7960 module, **NORMATIVE**: `R_EN` and `L_EN` are tied
together per module and driven by one shared MCU enable line (mirrors the previous "shared STBY"
concept — see §2.2). While the driver stage is enabled:

| Commanded state | `RPWM` | `LPWM` | Resulting motor behaviour |
|---|---|---|---|
| Forward, magnitude *m* | duty = *m* | 0 | forward at speed *m* |
| Reverse, magnitude *m* | 0 | duty = *m* | reverse at speed *m* |
| Zero / hold | 0 | 0 | **both low-side switches conduct — this is an active (short) brake**, not a coast |

Driving the shared enable line low de-asserts both `R_EN`/`L_EN` on both modules, which is the
*only* way to get a true high-impedance coast with this driver — this is what phase 3 of the
safe-stop sequence (§6, "motor enable de-asserted") does, and it is also the disabled state for
every non-driving vehicle state (`INIT`/`DISARMED`/`ARMED_IDLE`/`STOPPED`/`FAULT`/`ESTOP`).
*Rationale: unlike the TB6612FNG truth table this spec previously assumed (where `AIN1=AIN2=0` is
Hi-Z coast), a BTS7960 held at `RPWM=LPWM=0` with its enables asserted is actively braking, because
both low-side FETs are on and short the motor terminals together. This is convenient — it means the
safe-stop brake phase (VEH-040 phase 2) and "throttle returned to zero mid-drive" collapse to the
same electrical state — but it is a real behavioural difference from the driver this spec originally
assumed, and it is why RUN/LIMITED momentarily braking instead of coasting whenever the setpoint
passes through zero is expected, not a bug.*

### 2.2 MCU peripheral allocation

**🟡 DESIGNED and implemented** in `vcs-mcxn947/` (builds clean, not yet flashed
— see that project's README for the full cross-reference trail). Every pin below is checked against
either an SDK reference example this workspace can build, or the FRDM-MCXN947 UM12018 Arduino
header tables (17–20) for conflicts — none are guessed.

| Function | Peripheral | Pin | Notes |
|---|---|---|---|
| Motor L RPWM / LPWM | PWM1 (eFlexPWM) SM0, channel A / B | PORT2_6 / PORT2_7 (J3-15 / J3-13), ALT5 | One submodule, 2 independent channels — VEH-001a |
| Motor R RPWM / LPWM | PWM1 (eFlexPWM) SM1 channel A / SM3 channel A | PORT2_4 / PORT2_0 (J3-11 / J3-1), ALT5 | SM1's own channel B is not header-accessible (routed to on-board FLEXSPI0 flash), so LPWM_R uses SM3 instead — see `board_port/pin_mux.c` |
| Driver EN (shared `R_EN`+`L_EN`, both modules) | GPIO | PORT0_28 (J2-D8), active-high | Defaults low (disabled) at reset — VEH-001a |
| CAN | FlexCAN0, classical mode | PORT1_10 (TXD) / PORT1_11 (RXD), ALT11 | See [04](04-interface-control-document.md); on-board TJA1057GTK/3Z transceiver, header J10 |
| Buzzer | PWM1 SM2, channel A (tone generation) | PORT2_2 (J3-7), ALT5 | Frequency-agile — `PWM_SetupPwm()` re-called only when the frequency actually changes |
| Status LED (R/G/B) | GPIO, **active-LOW** on this board | PORT0_10 / PORT0_27 / PORT1_2 | Matches `../touch_rgb`, `../wifi_sensing_npu` in this workspace |
| Hazard lamps | GPIO ×2 | PORT0_25 / PORT4_0 (J2-D13/D18) | 1 Hz, both together (not alternating L/R) |
| Vibration motor | GPIO + low-side switch | PORT0_24 (J2-D11) | |
| Fan relay | GPIO + opto/relay module | PORT0_26 (J2-D12) | Flyback protection mandatory |
| ACK button | GPIO with pull-up + ≥20 ms debounce | PORT4_1 (J2-D19) | |
| Operator re-arm | GPIO with pull-up | PORT2_3 (J3-5) | One physical button serves both the initial arm and every later re-arm (see safety.h) |
| E-stop sense | GPIO with pull-up | PORT2_5 (J3-9) | Sense only — the actual cut is in hardware (VEH-074) |
| Motor current sense | LPADC | **not wired** | OI-05-01 — firmware treats a 0 reading as "not wired", never as a fault |
| Watchdog | WWDT0, 500 ms | — | |
| Console | LPUART (FLEXCOMM4), 115200 8N1 | PORT1_8/9 | Standard debug console pins on this board |

**VEH-003** — All peripheral clock gates and resets SHALL be explicitly enabled at init
(see [02 DEV-032](02-development-standards.md#41-platform-specific-rules-learned-the-hard-way)). A
peripheral with no clock reads back zeros and produces no error.

**VEH-004** — Pin assignment SHALL avoid PIO1_3, which is connected to the board's touch electrode
through R156, and SHALL be checked against `pin_mux.c` before wiring, not after. (The pin table
above has no PIO1_3 use; that constraint carries over from `touch_rgb`/`wifi_sensing_npu` in the
same workspace even though this project doesn't use the touch pad itself.)

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
| ~~OI-05-03~~ | ~~Decide TB6612FNG vs L298N and record the deviation if L298N~~ **Closed** — driver decided as BTS7960 (VEH-001); no L298N/TB6612FNG deviation applies. | HW | Closed 2026-08-14 |
| OI-05-04 | Verify 85 dB(A) limit by measurement | Test | Before demo |
| ~~OI-05-05~~ | ~~Confirm chosen pins do not collide with `pin_mux.c` defaults or PIO1_3~~ **Closed** — full pin assignment implemented in `vcs-mcxn947/board_port/pin_mux.c`, cross-checked against UM12018 Tables 17–20 and the SDK's own reference examples; builds clean. Physical wiring itself is still pending. | FW | Closed 2026-08-10 |
| OI-05-06 | Characterise the actual deceleration profile from the 1500 ms duty ramp (VEH-041) | Test | Before demo |

---

## Revision history

| Rev | Date | Author | Change |
|---|---|---|---|
| 0.1 | 2026-08-10 | ML_IoT_Love50 | Initial baseline |
| 0.2 | 2026-08-10 | ML_IoT_Love50 | §2.2 pin table replaced with the implemented, cross-referenced assignment from `vcs-mcxn947/`. OI-05-05 closed. Firmware builds clean; not yet flashed/measured (no §10/§8 items below are closed by this). |
| 0.3 | 2026-08-14 | ML_IoT_Love50 | Driver decided as BTS7960 (VEH-001/VEH-001a), replacing the TB6612FNG/L298N choice. §2.2 pin table updated: `RPWM`/`LPWM` per channel (PWM1 SM0 A/B for left, SM1 A + SM3 A for right) replace the single-PWM+direction-GPIO interface; shared STBY becomes shared `R_EN`/`L_EN` enable. OI-05-03 closed. `vcs-mcxn947/src/motion` and `board_port/` updated to match. |
