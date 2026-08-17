# vcs-mcxn947 — Vehicle Control Simulator node (FRDM-MCXN947)

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

**Status: simplified + flashed on real hardware.** Builds clean (0 warnings, `-Werror`). CAN link
to a real peer (Arduino UNO Q, `../dms-ap-uno-q/`) confirmed bidirectional on real hardware.
**2026-08-15: scope trimmed to match the system block diagram exactly** — vibration motor, fan
relay, hazard lamps, and the ACK/operator-re-arm/e-stop-sense buttons were removed; gas/brake
(onboard SW2/SW3) and auto re-arm replace them. See "What changed" below and
[`PINOUT.md`](PINOUT.md) for the current (and only) pin reference.

## Flashing and debugging

```bash
./build.sh            # build, then flash
./build.sh build      # build only
./build.sh flash      # flash the last build
./build.sh rebuild    # clean build from scratch
./build.sh reset      # reset the board without reflashing
./build.sh erase      # mass-erase the chip (recovery, e.g. after a bad flash config)
./build.sh monitor    # serial console (115200 8N1) -- tio, then picocom, then screen, first one found
```

Flashing goes over the on-board MCU-Link probe (CMSIS-DAP) via `pyOCD`; the same probe also enumerates
a USB-CDC virtual COM port for the debug console (`PRINTF` output + the UART CAN simulator below) —
one USB cable does both. Default serial device is `/dev/ttyACM0`; override with `SERIAL_PORT=/dev/ttyACM1
./build.sh monitor` if something else claims `ttyACM0` first (e.g. another board already plugged in).

**First-time setup / probe permissions** — same mechanics as `touch_rgb/README.md` in `NPX_Workspace`,
not repeated in full here, but in short: `build.sh` falls back to `sudo` automatically if pyOCD can't
open the probe without it (you'll see it say so). To flash without sudo, install a udev rule once:

```bash
sudo tee /etc/udev/rules.d/50-cmsis-dap.rules > /dev/null <<'EOF'
SUBSYSTEM=="usb", ATTR{idVendor}=="1fc9", MODE="0660", TAG+="uaccess"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="1fc9", MODE="0660", TAG+="uaccess"
EOF
sudo udevadm control --reload-rules && sudo udevadm trigger
```

then unplug/replug the board. If more than one CMSIS-DAP probe is attached, `build.sh` already
selects the one identifying as `MCU-LINK` by USB product string, so a second board or an ST-Link/
J-Link on the same machine doesn't need `-u <uid>` passed manually.

### Suggested first-flash / debug loop

1. `./build.sh build` — confirm 0 warnings under `-Werror` before touching hardware at all.
2. Plug the FRDM-MCXN947 in via the **MCU-Link USB port** (not the target USB port).
3. `./build.sh flash` — flashes and resets the board; it starts running immediately.
4. `./build.sh monitor` in a separate terminal (or a GUI serial tool, see below) — you should see the
   boot banner (`=== DrowsyGuard VCS -- FRDM-MCXN947 ===`), the WWDT arm line, `[motion] PWM1
   SM0/SM1/SM3 up at 20000 Hz, driver stage disabled`, and the `[sim] CAN frame simulator --
   commands:` help text from `sim_uart` — if you see all of that, CAN/PWM/watchdog bring-up and the
   debug console are all confirmed working before a single wire to a motor or a second CAN node.
5. Drive the state machine with the UART simulator (see the section above) and confirm the LED/buzzer
   pattern and `status` output match spec 05 §5 for each level, without anything else attached.
6. Once BTS7960 modules + motors are wired, watch `status`'s `dutyL`/`dutyR`/`cap` fields track VEH-020
   as you cycle `l0`→`l3`, and confirm the safe-stop ramp/brake timing at `l3` against VEH-040's 2.0 s
   budget (a stopwatch on the console log timestamps is enough for a first pass; the logic-analyser
   method in spec 06 §4 is for the recorded benchmark numbers, not for this kind of bring-up check).
7. If nothing prints at all: check `SERIAL_PORT` matches what `ls /dev/ttyACM*` shows, check the baud
   is 115200 8N1, and check you're on the MCU-Link USB port. If the board resets in a loop, `./build.sh
   monitor` will show `*** reset cause: WWDT0 timeout ***` from `safety.c` — that means some task is
   stalling past 500 ms (VEH-052), not a UART problem.

**GUI serial terminal alternative:** any 115200 8N1 terminal works instead of `./build.sh monitor` —
e.g. [Serial Studio](https://serial-studio.github.io/) (built from source, GPLv3) gives scrollback,
search and multiple simultaneous views of the same log, which is handy once you're also watching
`status` output while typing `l0`..`l3` commands.

## What this firmware actually does

- Brings up **FLEXCAN0 at 500 kbit/s** and speaks the exact wire format in
  [spec 04](../specs/04-interface-control-document.md): decodes `DMS_STATUS` /
  `DMS_METRICS`, transmits `VCS_STATUS` at 10 Hz, triple-sends `EMERGENCY_STOP` per CAN-040, runs
  the CRC-8 + timeout supervisor (300 ms degrade / 1000 ms safe-stop, CAN-060..066).
- Runs the **vehicle state machine** from spec 05 §3 (`INIT → DISARMED → ARMED_IDLE → RUN ⇄
  LIMITED → DECEL → STOPPED`, plus `FAULT`/`ESTOP`): absence of a valid CAN frame is never treated
  as "driver is fine" (CAN-063), a watchdog reset always comes up disarmed (VEH-053). **Re-arm out
  of STOPPED/FAULT/ESTOP is automatic** (2026-08-15) — no operator button exists on this board
  anymore, so `safety.c`'s `SafeConditionsSustained()` auto-recovers once the CAN link is OK and
  the alert level has been back at L0/Normal continuously for 1 s. This is a deliberate trade
  against spec 05 VEH-012's original "level alone never leaves STOPPED" principle, not an
  oversight — see `PINOUT.md`'s "Removed" section.
- Reads **gas (SW2) / brake (SW3)**, the board's own onboard buttons, and ramps a persistent
  throttle setpoint accordingly (20 %/s accelerate, 40 %/s decelerate, mutually exclusive, brake
  wins on a simultaneous press) — feeds `ARMED_IDLE → RUN` (VEH-011) and `dg_throttle_setpoint_t`.
- Drives **2 independent motor channels** (differential drive) through 2 BTS7960 half-bridge
  modules — 4 PWM outputs total (`RPWM`/`LPWM` per channel) plus one shared enable GPIO, per spec
  05 VEH-001/VEH-001a — with the speed cap table, 40 %/s ramp limiter, and the 1500 ms ramp + 500 ms
  brake + disable safe-stop sequence from spec 05 §4/§6.
- Runs the **alert pattern engine** from spec 05 §5 — buzzer tone (frequency-agile PWM) + 3-colour
  status LED only (vibration/fan/hazard removed 2026-08-15, not in the system block diagram) — as
  one non-blocking state machine driven by the 10 ms control tick (no `delay()` anywhere in the
  alert or motion path).
- Services a **500 ms window watchdog (WWDT0)** from the control task only, after a full iteration
  completes, and detects+reports a watchdog-induced reset at boot.
- Runs a **UART CAN-frame simulator** (`src/sim_uart/`) on the same debug console UART, so the
  whole safety/motion/alerts chain can be driven from a serial terminal alone — no second CAN node,
  no DMS-AP, no camera. See "Simulating CAN traffic over UART" below.

## Simulating CAN traffic over UART (no second node needed)

The board only becomes interesting once something is sending `DMS_STATUS` — without it, `link`
stays `LINK_LOST` forever (CAN-063) and the vehicle can never leave `DISARMED`. Normally that
"something" is the DMS-AP node or `tools/can_inject.py` (spec 06 §3.2) talking over the physical
CAN bus. `src/sim_uart/` gives you a third option that needs nothing but the USB cable already
plugged in for the debug console: type commands into the same serial terminal that shows the
`PRINTF` log, and the firmware injects a synthetic, already-decoded `DMS_STATUS`/`EMERGENCY_STOP`
directly into `can_link.c`'s state, re-sent every 100 ms like a real DMS-AP would.

**This does not exercise FLEXCAN0 itself** — no mailbox, no CRC, no bus timing. It's a firmware-side
shortcut for exactly the situation you're in right now (one board, no second node, no CAN adapter).
Once a real DMS-AP or `tools/can_inject.py` is available, use that instead to validate the actual
CAN path — don't run both into the bus at once (see `can_link.h`).

Connect with `./build.sh monitor` (or any serial terminal, see "Flashing and debugging" above),
then type `help` for the full list. The essentials:

| Command | Effect |
|---|---|
| `l0` / `l1` / `l2` / `l3` | Set alert level, start injecting at 100 ms |
| `calib on` / `calib off` | Set `flag_calib_done` for future frames (needed for `DISARMED → ARMED_IDLE`, VEH-011) |
| `sensorlost on` / `off` | Set `flag_sensor_lost` — should produce the silent blue-LED `SENSOR_LOST` alert (VEH-032), not a drowsiness alarm |
| `pause` | Stop injecting — link degrades then goes `LINK_LOST`, exactly like a dead bus |
| `resume` | Resume injecting the last level |
| `estop 1\|2\|3\|4` | Inject `EMERGENCY_STOP` (1=physical 2=dms 3=vcs 4=operator) |
| `status` | Print `vehicle_state` / motion duty / link state / faults as text |

A typical bring-up sequence once flashed: `calib on` auto-arms into `ARMED_IDLE` (no button needed
anymore, see "What this firmware actually does"), hold **SW2 (gas)** to reach `RUN`, `l2` and
watch the amber/red LED start, `l3` and watch the safe-stop ramp → brake → `STOPPED` — it
auto-recovers back to `ARMED_IDLE` on its own once `l0` has held for 1 s (no button press needed).

## Pin assignment

**See [`PINOUT.md`](PINOUT.md) — the single source of truth for every pin on this board**,
cross-referenced against UM12018 (FRDM-MCXN947 Board User Manual) Tables 4/17-19, matching the
system block diagram exactly (CAN, 2x BTS7960, buzzer+LED, gas/brake — nothing else). The DMS
side (Arduino UNO Q, external CAN transceiver + its own D4/D5 pins) is documented in
`../dms-ap-uno-q/README.md`.

## What is NOT wired yet (do not treat as done)

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
- **BTS7960 zero-duty is an active brake, not a coast** (spec 05 VEH-001a): `RPWM=LPWM=0` with the
  shared enable asserted shorts both motor terminals through the low-side FETs. This is deliberate
  for the safe-stop brake phase, but it also means the vehicle brakes (not coasts) any time the
  commanded setpoint passes through zero during normal `RUN`/`LIMITED` driving. True coast only
  happens when the shared enable GPIO is de-asserted (every non-driving state).

## Source layout

```
vcs-mcxn947/
├── board_port/          # pin_mux.c/h, cm33_core0/{app.h,hardware_init.c,prj.conf}
├── src/
│   ├── main.c            # boots everything, owns can_rx_task/control_task/alert_task/telemetry_task
│   ├── icd/               # wire format, hand-mirrors ../../shared/icd/icd.yaml (DEV-002 — see
│   │   │                    ../../shared/icd/README.md for exactly what's generated vs hand-written)
│   │   ├── icd.h / icd.c    encode/decode for every spec-04 message
│   │   └── crc8.h / crc8.c  CRC-8 SAE-J1850 + self-test vectors, copied from
│   │                        ../../shared/icd/crc_vectors.csv (CAN-070)
│   ├── can_link/           # FlexCAN0 driver, timeout supervisor, event repeaters
│   ├── safety/             # vehicle state machine, watchdog, fault evaluation
│   ├── motion/             # PWM motor drive, speed governor, safe-stop sequencer
│   ├── alerts/              # buzzer/LED pattern engine
│   └── sim_uart/            # UART CAN-frame simulator (bring-up aid, not in VEH-060's task table)
└── build.sh               # same build/flash pattern as NPX_Workspace/{touch_rgb,wifi_sensing_npu}
```

The CMake project (and the .elf it produces) is named `vcs_mcxn947` (underscore — CMake target
naming convention in this workspace), while the directory and repo-facing name use a hyphen
(`vcs-mcxn947`) to match [specs/02 §2](../specs/02-development-standards.md#2-repository-layout)'s
repository layout exactly.

## Next steps (in order)

1. Flash and confirm bring-up with **nothing else attached**, driving the state machine purely
   through the UART simulator (see above) — this alone confirms CAN/PWM/watchdog init, the FSM
   transitions, and the alert patterns, before any wiring risk.
2. Bring up CAN on the physical bus alone: `04 §10 bring-up checklist` — resistance check, scope
   levels, bit-time measurement — **before** connecting a second node.
3. Wire the BTS7960 motor driver stage and re-measure `MOTION_MIN_MOVE_DUTY` on the loaded chassis
   (`TC-VEH-001`).
4. Get the DMS-AP (Arduino UNO Q, `../dms-ap-uno-q/`) side transmitting real `DMS_STATUS` frames
   over the physical bus — **done, confirmed working 2026-08-15** — to validate the actual
   FLEXCAN0 RX path (mailboxes, CRC, bus timing), which the UART simulator in step 1 deliberately
   bypasses.
5. ~~Wire a real throttle/enable input~~ — **done**: gas/brake (onboard SW2/SW3), see
   `PINOUT.md`.
