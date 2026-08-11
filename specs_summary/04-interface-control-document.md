# Summary of 04 — Interface Control Document (CAN)

Source: [specs/04-interface-control-document.md](../../specs/04-interface-control-document.md)

## Physical layer
- Classical CAN 2.0A (not CAN FD, even though both chips support it — payload is only 8 bytes,
  FD would just add bit-timing complexity for no benefit).
- 500 kbit/s, sample point 87.5%, **120Ω termination at each end** (measures 60Ω±5Ω with power
  off — a bus with 1 or 3 termination points "seems to work" at short range then fails intermittently).
- 3.3V logic both sides. Bus ≤2m (bench), well inside the 40m theoretical limit at this rate.
- **VCS side confirmed:** FlexCAN0, PORT1_10/11, **on-board TJA1057GTK/3Z transceiver** — no
  external part needed, builds clean in `vcs-mcxn947/`, not yet flashed/measured.
- **DMS side still ⚠️ ASSUMPTION:** whether FDCAN1 on the STM32U585 is reachable on the UNO Q's headers.

## Message catalogue (lower ID = higher priority in arbitration)
| ID | Name | Direction | DLC | Cycle | Note |
|---|---|---|---|---|---|
| `0x080` | `EMERGENCY_STOP` | both ways | 2 | event, ≤3× @10ms | Absolute priority |
| `0x100` | `DMS_STATUS` | DMS→VCS | 8 | **100ms** | The most important safety message |
| `0x101` | `DMS_METRICS` | DMS→VCS | 8 | 500ms | Telemetry only, doesn't influence actuation |
| `0x200` | `VCS_STATUS` | VCS→DMS | 8 | **100ms** | Vehicle state feedback |
| `0x201` | `VCS_EVENT` | VCS→DMS | 2 | event | Ack, re-arm, e-stop |
| `0x700/0x701` | `DIAG_REQ/RESP` | | 8 | on request | Lowest priority |

Total bus load ≈ **0.6%** of 500kbit/s (requirement ≤5%) — huge headroom, deliberately: the bus
must never be the reason a safety message is late.

## `0x100 DMS_STATUS` — the only message the VCS actually acts on
- Byte 0: `alert_level` (0-3) + 4-bit `seq`.
- Byte 1: D1/D2/D3 states (2 bits each) + `d3_avail` (available/degraded/unavailable).
- Byte 2: `perclos_pct` (255=invalid). Bytes 3-4: `eye_closure_ms`. Byte 5: `face_conf_pct`.
- Byte 6: flags (ack_refractory, sensor_lost, model_degraded, night_mode, calib_done,
  pipeline_slow, ack_saturated).
- Byte 7: CRC-8 SAE-J1850 over bytes 0-6.
- **`alert_level` is the ONLY field the VCS acts on** — every other field is for
  indication/logging/diagnostics.
- Validation order: DLC==8 → CRC matches → seq is correct next value. Any failure → **discard,
  do NOT refresh the timeout supervisor** (a corrupted frame that still refreshes the watchdog
  is worse than a lost frame — it masks a dying link with stale data).
- Until `flag_calib_done=1`, the VCS remains disarmed regardless of `alert_level`.

## Other messages
- `0x200 VCS_STATUS`: vehicle state (INIT/DISARMED/ARMED_IDLE/RUN/LIMITED/DECEL/STOPPED/
  LINK_LOST/FAULT/ESTOP), speed cap, left/right duty + direction, fault flags, indicator (used
  by D1's gaze-suppression when the turn signal is on).
- `0x201 VCS_EVENT`: ack/re-arm/e-stop/indicator — sent **3 times at 10ms intervals** with the
  same `event_seq`, receiver de-dups by seq (an event has no natural repetition to recover it —
  losing a single ACK frame means the driver pressed the button and got ignored, exactly the
  experience that destroys trust).
- `0x080 EMERGENCY_STOP`: has a `magic=0x5A` byte so a spurious short frame at the highest-
  priority ID can't accidentally stop the vehicle. **This is a convenience path, not the
  official safety path** — real safety is the physical switch that cuts motor power (works even
  if firmware is hung).

## Timeout supervision — the most important section of this document
| Milestone | Value | Action |
|---|---|---|
| Nominal cycle | 100ms | |
| Degrade (3 missed cycles) | **300ms** | Enter `LINK_LOST`, cap speed to 30%, amber warning |
| Safe-stop | **1000ms** | Execute a full safe-stop |
| Recovery | 5 consecutive valid frames | Re-enter at the **newly received** level, not the pre-fault level |

- **The classic bug to avoid:** absence of `DMS_STATUS` must never be interpreted as
  `alert_level=L0`. Holding the last value forever makes a dead link look exactly like a
  perfectly alert driver — silent, passes every functional test, and only shows up when the
  cable falls out during the demo.
- Bus-off must recover automatically and be counted/logged; a demo that experiences a bus-off is not clean.

## Open items (risks to track)
- **OI-04-01** (⚠️ highest risk): whether FDCAN on the UNO Q reaches the headers — decided
  within 24h of hardware arrival. Fallback: SPI CAN (MCP2515-class, +1ms latency, +1 day
  bring-up), then UART framed protocol.
- OI-04-03/04/05: bit-timing, CRC-8 implementation, transceiver — **VCS side resolved**, DMS
  side still open (no DMS-side CRC code yet to cross-verify).

## Bring-up checklist (do in order, don't skip ahead)
Measure bus resistance 60Ω±5Ω with power off → scope CAN_H/L → transmit from 1 node into a
terminated bus with no second node (confirm no ACK is expected) → measure bit time = 2.00µs±1%
→ both nodes on bus, error counters at 0 for 60s → CRC test vector matches both builds → drive
the VCS through every level with `can_inject.py`, no DMS attached → unplug the bus mid-run,
confirm `LINK_LOST` at 300ms and safe-stop at 1000ms with a scope.
