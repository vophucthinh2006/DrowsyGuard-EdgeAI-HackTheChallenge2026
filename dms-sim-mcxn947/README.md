# dms-sim-mcxn947 — hardware CAN test node (FRDM-MCXN947)

Not part of DrowsyGuard's product firmware. This is a **bring-up/test tool**: flash it onto a
*second* FRDM-MCXN947 and it plays the DMS-AP side of
[../vcs-mcxn947](../vcs-mcxn947/README.md)'s CAN link on the **real physical bus** — real
FLEXCAN0 mailboxes, real CRC-8, real bus timing — so you can validate `vcs-mcxn947`'s CAN RX
path with two boards on a bench, without the DMS-AP (Arduino UNO Q / STM32U585) hardware.
This is exactly the "or use `tools/can_inject.py`" alternative `vcs-mcxn947/README.md`'s
"Next steps" step 4 calls for, just done as a second firmware instead of a host-side script.

It deliberately does **not** touch `vcs-mcxn947/src/` — it's a sibling project with its own
copy of the ICD (`src/icd/`), because `vcs-mcxn947`'s ICD only implements the encode/decode
directions the real VCS firmware needs (it encodes `VCS_STATUS`/`VCS_EVENT`, never decodes
its own outgoing messages). This node needs the opposite subset, so `src/icd/icd.h`/`.c` add
`DG_DecodeVcsStatus()` / `DG_DecodeVcsEvent()` and `DG_EncodeDmsStatus()` /
`DG_EncodeDmsMetrics()` on top of an unmodified copy of `crc8.c` (must stay byte-identical
between both nodes — see `crc8.c`'s header comment on why it carries self-test vectors).

## Wiring

1. Both boards: FRDM-MCXN947, same on-board TJA1057GTK/3Z CAN transceiver, header **J10**
   (`CAN1_H`, `CAN1_L`, `P5V0`, `GND` — silkscreen label, wired to FLEXCAN0 per both boards'
   `board_port/pin_mux.c`).
2. Connect `CAN_H` to `CAN_H`, `CAN_L` to `CAN_L`, `GND` to `GND` between the two boards.
3. Add **120 Ω termination resistors** across `CAN_H`/`CAN_L` at each physical end of the bus
   (CAN-002) — a simple 2-board bench setup with a short wire between the two J10 headers is
   both physical ends, so one 120 Ω resistor at each board's J10 is normally enough. Skipping
   this is the most common reason a first bring-up shows nothing on either side.
4. Each board needs its **own** USB cable to its own MCU-Link port (power + debug console +
   flashing) — the CAN wires between the boards carry no power.

## Build / flash / run

Same `build.sh` verbs as `vcs-mcxn947`, plus `PROBE_UID` because two MCU-Link probes are
plugged in at once during a CAN bring-up session — see `build.sh`'s header comment for how to
find each board's UID.

```bash
./build.sh build                                        # build once
PROBE_UID=<uid-of-dms-sim-board> ./build.sh flash        # flash the dms-sim board specifically
PROBE_UID=<uid-of-dms-sim-board> SERIAL_PORT=/dev/ttyACM1 ./build.sh monitor
```

In parallel, on the vcs-mcxn947 board: `../vcs-mcxn947/build.sh flash` +
`../vcs-mcxn947/build.sh monitor` as normal (its own default `PROBE_UID` picks whichever
MCU-Link comes first if only one is plugged in — pin it explicitly too once both are attached).

## Fully autonomous — no console, no commands

`src/console/` was removed on purpose (2026-08-15): this node now starts transmitting a real
`DMS_STATUS` frame (alert level L0, fixed fields) every 100 ms **the instant it boots**, no
typed command needed. Every valid `VCS_STATUS` / `VCS_EVENT` / `EMERGENCY_STOP` frame received
is printed live as it arrives, and a one-line counter summary prints every 1 s:

```
[count] tx_dms_status=120 rx_vcs_status=118 rx_vcs_status_seq_gaps=0 rx_vcs_event=0 ms_since_last_rx=42
```

That line *is* the validation — `tx_dms_status` and `rx_vcs_status` climbing together every
second with `rx_vcs_status_seq_gaps=0` confirms the real FLEXCAN0 path on both boards is working
end to end (mailboxes, CRC, bus timing), not just the UART-simulator shortcut in
`vcs-mcxn947/src/sim_uart/`. Just flash and open `./build.sh monitor` (or any 115200 8N1
terminal) — nothing to type.

The on-board RGB LED (active-low, same pins as `vcs-mcxn947`) doubles as a link-fresh
indicator with no serial terminal needed: **green** = a valid `VCS_STATUS` was received within
the last 500 ms, **red** = stale or never received.

## What this does NOT do

- No safety FSM, no motors, no alerts — this is a bus-level test peer only, not a second VCS.
- `DG_DecodeVcsStatus()`/`DG_DecodeVcsEvent()` here are a hand-written mirror of
  `vcs-mcxn947/src/icd/icd.c`'s encoders, not shared code — if the wire format changes on the
  VCS side, update this copy too (same DEV-092 hand-sync discipline `vcs-mcxn947` already
  documents for its own ICD copy).
- No way to trigger `EMERGENCY_STOP`/`DMS_METRICS` TX or vary the alert level right now — the
  console that exposed those was deliberately deleted for a pure "count real packets crossing
  the bus" test (`CanTestLink_TransmitEmergencyStop()`/`TransmitDmsMetrics()` are still in
  `can_test_link.c`, just unused; wire a command surface back in, or call them directly from
  `main.c`, if you need to test those paths again).
