# drowsyguard_vcs — Vehicle Control Simulator node (FRDM-MCXN947)

The VCS half of **DrowsyGuard** (Qualcomm Future Makers 2026, team ML_IoT_Love50). Full spec
set: [`../specs/`](../specs/README.md) — this firmware implements
[01](../specs/01-system-requirements.md),
[04](../specs/04-interface-control-document.md) and
[05](../specs/05-vehicle-control-spec.md) on real MCXN947 hardware.

**Source lives in this repo; the toolchain does not.** This project builds against an
MCUXpresso SDK checkout + west venv + CMSIS packs (~4+ GB) that intentionally stay **outside**
this git repo, in a shared local workspace (`NPX_Workspace`, default `~/embedded/NPX_Workspace`,
override with `NPX_WORKSPACE=...`) — the same one used by two other MCXN947 projects on that
machine, `touch_rgb` and `wifi_sensing_npu`, whose build/pin-mux conventions this project follows.
See `build.sh` for exactly how it's located.

**Status: bring-up build.** Builds clean (0 warnings, `-Werror`), not yet flashed/tested on the
physical board+motors+CAN bus. See "What is NOT wired yet" below before treating anything here as
verified.

## Build & flash

```bash
./build.sh            # build, then flash
./build.sh build      # build only
./build.sh flash      # flash the last build
./build.sh monitor    # serial console (115200 8N1)
```

Same probe-pinning / sudo-udev-rule mechanics as `touch_rgb/README.md` in `NPX_Workspace` —
not repeated here.

## What this firmware actually does

- Brings up **FLEXCAN0 at 500 kbit/s** and speaks the exact wire format in
  [spec 04](../specs/04-interface-control-document.md): decodes `DMS_STATUS` /
  `DMS_METRICS`, transmits `VCS_STATUS` at 10 Hz, triple-sends `VCS_EVENT` (ACK / re-arm) and
  `EMERGENCY_STOP` per CAN-040, runs the CRC-8 + timeout supervisor (300 ms degrade / 1000 ms
  safe-stop, CAN-060..066).
- Runs the **vehicle state machine** from spec 05 §3 (`INIT → DISARMED → ARMED_IDLE → RUN ⇄
  LIMITED → DECEL → STOPPED`, plus `FAULT`/`ESTOP`), with the safety rules that matter most:
  absence of a valid CAN frame is never treated as "driver is fine" (CAN-063), a completed safe
  stop never resumes on alert level alone (VEH-012), a watchdog reset always comes up disarmed
  (VEH-053).
- Drives **2 independent PWM motor channels** (differential drive) through a TB6612FNG-style
  H-bridge interface, with the speed cap table, 40 %/s ramp limiter, and the 1500 ms ramp + 500 ms
  brake + disable safe-stop sequence from spec 05 §4/§6.
- Runs the **alert pattern engine** from spec 05 §5 — buzzer tone (frequency-agile PWM), 3-colour
  status LED, vibration, fan relay, hazard lamps — as one non-blocking state machine driven by the
  10 ms control tick (no `delay()` anywhere in the alert or motion path).
- Services a **500 ms window watchdog (WWDT0)** from the control task only, after a full iteration
  completes, and detects+reports a watchdog-induced reset at boot.

## Pin assignment (verified, not guessed)

Every physical pin below is cross-referenced against a real source — either the SDK's own
`examples/_boards/frdmmcxn947/driver_examples/...` reference examples (which the `NPX_Workspace`
toolchain can build against directly, so the ALT values are known-correct for this exact SDK
checkout) or the **FRDM-MCXN947 User Manual (UM12018) Arduino header tables** (17–20), cross-checked
for the "Potential conflict" column so nothing here collides with the on-board RGB LED or MCU-Link
debug UART. See `board_port/pin_mux.c` for the per-pin comment trail.

| Signal | Pin | Peripheral | Source |
|---|---|---|---|
| CAN0 TXD | PORT1_10 (ALT11) | FLEXCAN0 | SDK `flexcan/interrupt_transfer` example + UM12018 "FlexCAN interface schematic" / Table 14 (`P1_10/CAN0_TXD`) — **matches the schematic screenshot supplied for this task** |
| CAN0 RXD | PORT1_11 (ALT11) | FLEXCAN0 | same as above (`P1_11/CAN0_RXD`) |
| Motor L speed | PORT2_6 / J3-15 (ALT5) | PWM1 SM0 A | SDK `pwm` (pwm_3ph) example |
| Motor R speed | PORT2_4 / J3-11 (ALT5) | PWM1 SM1 A | SDK `pwm` example |
| Buzzer tone | PORT2_2 / J3-7 (ALT5) | PWM1 SM2 A | SDK `pwm` example |
| *(spare hardware PWM)* | PORT2_7 / J3-13 (ALT5) | PWM1 SM? B0 | unused — reserved, e.g. future vibration intensity |
| Status LED red | PORT0_10, active-low | GPIO0.10 | matches `touch_rgb`, `wifi_sensing_npu` in `NPX_Workspace`; UM12018 Table 18 J2-4 `LED_RED` |
| Status LED green | PORT0_27, active-low | GPIO0.27 | same; UM12018 Table 18 J2-6 `LED_GREEN` |
| Status LED blue | PORT1_2, active-low | GPIO1.2 | same; UM12018 Table 17 J1-14 `LED_BLUE` |
| Motor L AIN1 / AIN2 | PORT0_29 / PORT1_23 — J1-D2 / J1-D3 | GPIO0.29 / GPIO1.23 | UM12018 Table 17, no listed conflict |
| Motor R BIN1 / BIN2 | PORT0_30 / PORT0_31 — J1-D4 / J1-D7 | GPIO0.30 / GPIO0.31 | UM12018 Table 17 |
| Driver STBY (shared enable) | PORT0_28 — J2-D8 | GPIO0.28, active-high | UM12018 Table 18 |
| Vibration motor | PORT0_24 — J2-D11 | GPIO0.24 | UM12018 Table 18 |
| Fan relay | PORT0_26 — J2-D12 | GPIO0.26 | UM12018 Table 18 |
| Hazard L | PORT0_25 — J2-D13 | GPIO0.25 | UM12018 Table 18 |
| Hazard R | PORT4_0 — J2-D18 | GPIO4.0 | UM12018 Table 18 |
| ACK button ("I am awake") | PORT4_1 — J2-D19, pull-up | GPIO4.1, active-low | UM12018 Table 18 |
| Operator re-arm | PORT2_3 — J3-5, pull-up | GPIO2.3, active-low | UM12018 Table 19 |
| E-stop sense | PORT2_5 — J3-9, pull-up | GPIO2.5 | UM12018 Table 19 — see wiring note below |

**On-board CAN transceiver:** the FRDM-MCXN947 already has a TJA1057GTK/3Z transceiver wired from
PORT1_10/11 out to header **J10** (`CAN1_H`, `CAN1_L`, `P5V0`, `GND`) — confirmed against the
board's own schematic, so **no external CAN transceiver is needed on the VCS side.** External
120 Ω termination is still required at each physical bus end per CAN-002 — the board does not
provide it.

Note this only resolves the VCS half of the link.
[Spec 04 OI-04-01](../specs/04-interface-control-document.md#9-open-items) — whether
the **DMS** side (STM32U585 on the Arduino UNO Q) exposes FDCAN on header-reachable pins — is
unrelated to this board and is **still open**; nothing in this session touched the DMS/UNO Q side.

**E-stop wiring note:** `ESTOP_SENSE` is pulled up in firmware and treated as "asserted" when the
line reads high. Per [spec 05 VEH-074](../specs/05-vehicle-control-spec.md#9-power-and-wiring)
the *authoritative* e-stop cuts the motor supply directly and does not depend on this firmware at
all — this GPIO is only how software finds out an e-stop happened, e.g. to report it over CAN and
refuse to silently resume.

## What is NOT wired yet (do not treat as done)

- **No physical throttle input.** `ARMED_IDLE → RUN` currently requires holding the re-arm button
  (see the `TODO` in `src/main.c`) — there is no pedal/lever signal defined. `dg_throttle_setpoint_t`
  in `ControlTask()` is a fixed bench value, not read from hardware.
- **Motor current/rail voltage sensing is not wired** ([spec 05 OI-05-01/OI-05-02](../specs/05-vehicle-control-spec.md#10-open-items)).
  `safety.c` treats a reading of exactly 0 as "not wired" and never raises `fault_driver` /
  `fault_undervoltage` from it — this is a deliberate guard, not a bug, but it means the stall and
  brown-out protections in spec 05 VEH-054/055 are currently inert.
- `MOTION_MIN_MOVE_DUTY` (25) and `I_STALL_LIMIT_MA` / `V_UNDERVOLT_MV` are **literature/placeholder
  values**, not measurements — see OI-05-01/02 in spec 05.
- No git-SHA/build-timestamp embedding yet (spec 02 DEV-071) — every boot banner should currently
  be read as `+dirty` for the purposes of [spec 02 DEV-072](../specs/02-development-standards.md#8-build)
  (no benchmark result may cite a run against this firmware until that's wired).
- `DIAG_REQ`/`DIAG_RESP` (0x700/0x701) are defined in the ICD header but not implemented — lowest
  priority per spec 04 §2.
- No simulated turn-indicator input, so `VCS_STATUS.indicator_active` is always false and the D1
  mirror-check suppression (CAN-030) has nothing to suppress against yet.
- **TB6612FNG vs L298N is assumed** ([spec 05 OI-05-03](../specs/05-vehicle-control-spec.md#10-open-items)):
  `MOTION_PWM_FREQUENCY_HZ` is 20 kHz. If the L298N fallback driver is used instead, drop this to
  8 kHz (VEH-002) or the PWM will run above what that driver handles efficiently.

## Source layout

```
drowsyguard_vcs/
├── board_port/          # pin_mux.c/h, cm33_core0/{app.h,hardware_init.c,prj.conf}
├── src/
│   ├── main.c            # boots everything, owns can_rx_task/control_task/alert_task/telemetry_task
│   ├── icd/               # wire format — the ONLY place message layouts are written (DEV-002)
│   │   ├── icd.h / icd.c    encode/decode for every spec-04 message
│   │   └── crc8.h / crc8.c  CRC-8 SAE-J1850 + self-test vectors (CAN-070)
│   ├── can_link/           # FlexCAN0 driver, timeout supervisor, event repeaters
│   ├── safety/             # vehicle state machine, watchdog, fault evaluation
│   ├── motion/             # PWM motor drive, speed governor, safe-stop sequencer
│   └── alerts/              # buzzer/LED/vibration/fan/hazard pattern engine
└── build.sh               # same build/flash pattern as NPX_Workspace/{touch_rgb,wifi_sensing_npu}
```

## Next steps (in order)

1. Bring up CAN alone: `04 §10 bring-up checklist` — resistance check, scope levels, bit-time
   measurement — **before** connecting a second node.
2. Wire the motor driver stage and re-measure `MOTION_MIN_MOVE_DUTY` on the loaded chassis
   (`TC-VEH-001`).
3. Get the DMS-AP (Arduino UNO Q) side transmitting real `DMS_STATUS` frames — even
   `tools/can_inject.py` sending synthetic ones is enough to exercise every state transition in
   `safety.c` without a camera in the loop (per spec 06 §3.2 "CAN frame injection").
4. Wire a real throttle/enable input and delete the bench-only `TODO` in `main.c`.
