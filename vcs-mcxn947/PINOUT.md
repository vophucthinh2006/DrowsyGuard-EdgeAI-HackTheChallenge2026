# vcs-mcxn947 — Pinout (single source of truth)

Matches the system block diagram exactly (2026-08-15 simplification — no vibration motor, fan
relay, hazard lamps, or ACK/re-arm/e-stop-sense buttons; those were removed from the firmware,
see `README.md`). Every pin below is cross-referenced against UM12018 (FRDM-MCXN947 Board User
Manual) Tables 4/17-19 and `board_port/pin_mux.c`'s own comments — not guessed.

## CAN (FLexCAN0 → real CAN bus)

Fully on-board, nothing to wire on the VCS side — `vcs-mcxn947` has its own TJA1057GTK/3Z
transceiver already wired to header **J10**.

| Signal | J10 pin |
|---|---|
| CAN_H | 1 |
| CAN_L | 2 |
| 5V | 3 |
| GND | 4 |

Connect `CAN_H`↔`CAN_H`, `CAN_L`↔`CAN_L`, `GND`↔`GND` to the other CAN node (e.g. the UNO Q side's
external transceiver). 120 Ω termination at each physical bus end (CAN-002).

## Motor driver (2x BTS7960 → 4 DC motors, differential drive)

| Signal | Module | BTS7960 pin | NXP connector | Pin # | NXP signal |
|---|---|---|---|---|---|
| RPWM left  | #1 (Left)  | `RPWM` | **J3** | **15** | `P2_6` (PWM1_A0) |
| LPWM left  | #1 (Left)  | `LPWM` | **J3** | **13** | `P2_7` (PWM1_B0) |
| RPWM right | #2 (Right) | `RPWM` | **J3** | **11** | `P2_4` (PWM1_A1) |
| LPWM right | #2 (Right) | `LPWM` | **J3** | **1**  | `P2_0` (PWM1_A3) |
| Enable (shared) | both, `R_EN`+`L_EN` all 4 pins tied together | — | **J2** | **2** | `P0_28` (GPIO, active-HIGH, silkscreen "D8") |
| VCC (logic) | both | `VCC` | **J3** | **10** | `P5V0` (5V) |
| GND (logic) | both | `GND` | **J3** | **12** or **14** | `GND` |
| `R_IS`, `L_IS` | both | — | — | — | **not connected** — current sensing not implemented |

Motor power (`B+`/`B-` on the green terminal block) and motor output (`M+`/`M-`) are **not**
connected to the NXP board at all — separate +12V supply, common GND with the NXP board and its
logic supply. Left motors (front+rear, wired in parallel) → module #1. Right motors → module #2.

`RPWM=LPWM=0` with enable asserted is an **active brake**, not coast (BTS7960 characteristic) —
deliberate for the safe-stop sequence.

## Local warning system (buzzer + RGB LED only)

| Signal | NXP connector | Pin # | NXP signal |
|---|---|---|---|
| Buzzer (PWM tone) | **J3** | **7** | `P2_2` (PWM1_A2) |
| Status LED red | onboard | — | `P0_10`, active-low |
| Status LED green | onboard | — | `P0_27`, active-low |
| Status LED blue | onboard | — | `P1_2`, active-low |

## Gas / brake (onboard buttons — no external wiring needed)

| Signal | Board button | NXP GPIO | Notes |
|---|---|---|---|
| Gas (accelerate) | **SW2** ("Wakeup" button) | `P0_23` | active-low, general-purpose input per UM12018 Table 4 |
| Brake (decelerate) | **SW3** ("ISP mode" switch) | `P0_6` | active-low. **Do not hold at power-on/reset** — sampled by the boot ROM to select ISP mode then; safe to read as GPIO any time after boot |

Both are physical push buttons already on the FRDM-MCXN947 board itself — nothing to plug in.

## Debug console (development only, not in the system diagram)

| Signal | NXP pin | Notes |
|---|---|---|
| TXD | `P1_8` | LPUART4, via onboard MCU-Link USB-CDC, `/dev/ttyACMx` |
| RXD | `P1_9` | 115200 8N1 |

## Removed 2026-08-15 (do not re-add without updating this file)

ACK button, operator re-arm button, physical e-stop-sense loop, vibration motor, fan relay,
hazard lamps (L/R) — none of these are in the system block diagram. Re-arm after
STOPPED/FAULT/ESTOP is now automatic (`safety.c`'s `SafeConditionsSustained()`), not
button-driven. Physical e-stop is purely electrical (cuts actuator +12V directly) and is not
sensed by this firmware at all — only a CAN-received `EMERGENCY_STOP` message still reaches
`ESTOP` state.
