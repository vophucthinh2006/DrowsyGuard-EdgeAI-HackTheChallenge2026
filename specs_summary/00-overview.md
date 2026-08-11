# DrowsyGuard — Quick Summary of the Specs Set

> Read this file first. Every other file in `specs_summary/en/` summarizes one file from
> `specs/`, enough to grasp the main ideas without reading the full document. Key numbers/IDs
> are kept intact for reference.

## What the project is

**DrowsyGuard** — an on-device driver-drowsiness monitoring system (no cloud) that detects
drowsiness via camera and intervenes on a **small-scale simulated vehicle**. Built for the
**Qualcomm Future Makers — Hack The Challenge 2026** (team ML_IoT_Love50), demo deadline
**2026-08-16**. Rev 0.1 was written **before hardware arrived** (the Arduino UNO Q is still on
loan) — every performance figure is a **budget/target**, not a measurement yet (0 ✅ VERIFIED entries).

## Two-node architecture, linked by CAN

```
DMS (Arduino UNO Q)                         VCS (NXP FRDM-MCXN947)
 ├─ DMS-AP: QRB2210, Linux, Python 3.13      ├─ FlexCAN0, motor control, safety
 │  → camera, MediaPipe face-landmark,       ├─ 4× TT gear motors (differential drive)
 │    fusion of 3 domains → alert level L0-L3├─ buzzer/LED/vibration/fan/hazard lights
 └─ DMS-RT: STM32U585, sends CAN             └─ speed cap + safe-stop driven by alert_level
        │
        └──── CAN 2.0A classical, 500 kbit/s, 2 nodes, 120Ω at both ends ────┘
```

**Separation-of-concerns principle:** the DMS decides "how drowsy is the driver"; the VCS
decides "what the vehicle does about it." The VCS **knows nothing** about vision/AI — it only
receives one number, `alert_level`, over CAN. The safety-critical path (CAN receive → speed
limit → safe-stop) runs entirely on the VCS MCU, independent of whether Linux is responsive.

## The 3 detection domains — the heart of the system

| Domain | Signal | Max level reachable alone | Meaning |
|---|---|---|---|
| **D1** Distraction | Eyes off road (yaw/pitch) | L2 | Behavioural — still awake but not looking at the road |
| **D2** Yawning | Yawn (MAR + duration) | L2 | Predictive — fatigue is building, not dangerous yet |
| **D3** Eye closure | Continuous closure + PERCLOS | **L3** | Imminent danger — only D3 may stop the vehicle |

4 alert levels: **L0 NORMAL → L1 EARLY → L2 DROWSY → L3 DANGER**. Escalation requires a *dwell
time* (the condition must hold continuously long enough) to prevent false alarms; de-escalation
is harder than escalation (hysteresis). L3 only clears via an operator re-arm — it never
recovers automatically.

## Current state (important context)

- **VCS side is fully designed and builds clean** (`vcs-mcxn947/`), but **not yet flashed or measured**.
- **DMS side (UNO Q) is still the biggest unknown**: it is not yet confirmed whether FDCAN is
  reachable on header pins (⚠️ ASM-01/OI-04-01) — the **single highest-risk item** in the whole
  system, with an SPI-CAN (MCP2515) fallback already planned.
- All thresholds in spec 03 come from **literature**, not yet tuned on the team's own corpus.
- There is also a **UI Dashboard** component (spec 09) — a desktop Electron/React app that
  mimics a car instrument cluster, receiving data over WebSocket from a Rust bridge that reads
  CAN. This is a demo/visualization layer, **not a safety controller**.

## Document map (read in this order if you need to go deeper)

| # | File | Content | Summary |
|---|---|---|---|
| 01 | system-requirements | Top-level requirements (functional/perf/safety) | [view](01-system-requirements.md) |
| 02 | development-standards | Coding, git, build, flashing rules | [view](02-development-standards.md) |
| 03 | drowsiness-domain-spec | D1/D2/D3 thresholds + rationale for every number | [view](03-drowsiness-domain-spec.md) |
| 04 | interface-control-document | Byte-exact CAN layout, timeouts | [view](04-interface-control-document.md) |
| 05 | vehicle-control-spec | Motors, alert actuators, safe-stop, failsafe | [view](05-vehicle-control-spec.md) |
| 06 | test-plan | Test strategy, rig, corpora | [view](06-test-plan.md) |
| 07 | test-cases | 130 concrete test cases (0 executed) | [view](07-test-cases.md) |
| 08 | benchmark-log | Log of real measurements (currently all "_pending_") | [view](08-benchmark-log.md) |
| 09 | ui-dashboard + dev-guide | Desktop app simulating the instrument cluster | [view](09-ui-dashboard.md) |

## Things easy to get wrong when working in this repo

1. **DEV-092**: when reality contradicts the spec → fix the spec in the same PR immediately,
   never leave a known-false number in a document. This already happened twice (the `dms-ap/`
   layout, and Python 3.13 instead of 3.11).
2. **DEV-014**: changes to `safety.c`, `can_rx.c`, `fusion/`, `shared/icd/`, or any threshold
   require **two approvals**, one from someone who didn't write the code.
3. The **ack button ("I am awake")** must never clear D3 CRITICAL (SYS-FR-015) — to avoid
   becoming a "defeat device." Ack also never resets the yawn/PERCLOS counters (it only
   silences the alert).
4. Every measured number needs a `run ID` + artefact directory; no artefact = not a result
   (BM-002). Never build `+dirty` when taking measurements (DEV-072).
5. "3 seconds = 100 meters" in the current script is **wrong** at the actual stated speed of
   90 km/h (should be 75 m) — see [08](08-benchmark-log.md) section 12, still unfixed as of
   this writing.
