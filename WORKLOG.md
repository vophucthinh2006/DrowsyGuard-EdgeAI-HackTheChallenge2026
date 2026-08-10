# DrowsyGuard — Work Log

A dated, narrative record of what happened and why. This is **not** a substitute for:
- `git log` — the actual code diffs and commit messages.
- [`specs/08-benchmark-log.md`](specs/08-benchmark-log.md) — raw measurement numbers with run IDs,
  the only place a performance/accuracy figure is allowed to be quoted from.
- Each subproject's own README (`dms-ap/README.md`, `vcs-mcxn947/README.md`, ...) — the current
  state of that piece, kept up to date in place, not a history.

This file is for the fourth thing none of those give you: **"what happened, in what order, and
why"** — so a teammate who was away for two days can read five minutes of log entries instead of
fifty commits and get caught up.

**Team:** Nguyen Hoang Trieu (Lead & Embedded), Tang Phon Thinh (AI & Computer Vision), Van Dac
Phong Truc (Software & Integration), Vo Phuc Thinh (Connectivity & Cloud) — HCMUT EE ML/IoT Lab.
**Deadline:** in-cabin prototype demo, **16 August 2026** (per the pitch's ask to Qualcomm).

---

## How to add an entry

New entries go at the **top** of the log (§3), most recent first. Keep it short: what changed,
why, and what it unblocks or blocks. If a change affects the status dashboard below, update the
dashboard in the same edit — a stale dashboard is worse than none.

```markdown
### YYYY-MM-DD — <short title>
**Who:** <name(s)>
- <bullet, what/why>
- <bullet>
**Unblocks / blocks:** <one line, or omit>
```

---

## 1. Status dashboard

*(Updated in place. Last updated: 2026-08-10.)*

| Component | Status | Blocking item |
|---|---|---|
| Specs (`specs/`) | 🟢 Complete, Rev 0.2 | Threshold values need re-tuning against a real corpus before acceptance (DOM-TUN-001) |
| VCS firmware (`vcs-mcxn947/`) | 🟡 Builds clean, unflashed | No FRDM-MCXN947 board in hand yet to flash/measure |
| DMS-AP domains/fusion (`dms-ap/app/python/drowsyguard/{domains,fusion}`) | 🟢 61/61 tests pass | Threshold re-tuning, same as specs |
| DMS-AP camera pipeline (`inference/blazeface_cnn_backend.py`) | 🟡 Code complete, never run | No camera/`cv2` in the dev environment used so far |
| DMS-AP ↔ DMS-RT link (`RouterBridgeTransport`) | 🟡 Implemented against docs, unverified | No UNO Q / App Lab available to test against |
| DMS-RT sketch (`dms-ap/app/sketch/sketch.ino`) | 🔴 Bridge link only, no FDCAN1 | Open Arduino-forum-level problem — see §2 |
| Head-pose / real MAR (`dms-ap/prototypes/face_mesh_ear_mar/`) | 🟡 Working prototype, not integrated | Needs a new `inference/face_mesh_backend.py` — **highest-leverage next task** |
| Shared ICD (`shared/icd/`) | 🟡 Documented, hand-synced | `generate.py` doesn't exist — DEV-002 |
| Benchmarks (`docs/benchmarks/`) | 🔴 Empty | 0 of 15 acceptance criteria measured — needs both boards in hand |
| Pitch deck arithmetic | 🔴 One error live | "90 km/h × 3 s = 100 m" is wrong (75 m); fix before recording, see §2 |

Legend: 🟢 done for now · 🟡 in progress / partially blocked · 🔴 not started or actively broken.

## 2. Known blockers, ranked by how much they block

1. **No hardware in hand.** Neither the Arduino UNO Q nor the FRDM-MCXN947 has been physically
   available while writing any of the code in this repo. Every "builds clean" / "61 tests pass"
   claim is real; every "verified on a device" claim would not be. Get boards, then work down this
   list top to bottom rather than trying to verify everything at once.
2. **FDCAN1 on the UNO Q sketch side is an open problem on Arduino's own platform**, not just this
   project — see `dms-ap/app/python/drowsyguard/link/README.md` for the forum thread. This blocks
   the DMS→VCS CAN link from ever going end-to-end.
3. **D1 (distraction) has no live head-pose sensor wired in.** A working implementation exists
   (`dms-ap/prototypes/face_mesh_ear_mar/`) but isn't integrated as an `InferenceBackend`. This is
   the single highest-value piece of remaining work on the DMS side — see `dms-ap/README.md`
   "Prototypes".
4. **Pitch script arithmetic error**, unrelated to code: `PITCH_SCRIPT_5MIN.md` says "three seconds
   at ninety kilometres an hour travels one hundred metres" — 90 km/h × 3 s = 75 m, not 100 m
   (100 m needs 120 km/h). It's the headline number, said twice. Fix the speed or the distance
   before recording. Tracked in `specs/08-benchmark-log.md` §12.

## 3. Log

### 2026-08-10 — dms-ap restructured into a real Arduino App Lab app
**Who:** Nguyen Hoang Trieu (+ Claude)
- Confirmed the previous `dms-ap/src/` layout would not load into Arduino App Lab at all — wrong
  folder shape entirely. Researched the real platform (App Lab's `app.yaml`+`python/`+`sketch/`
  structure, the Arduino_RouterBridge AP↔RT mechanism) against actual published examples, not
  guesses.
- Moved `dms-ap/src/drowsyguard/` → `dms-ap/app/python/drowsyguard/`; `config/` and `models/`
  moved inside `app/python/` too (App Lab only deploys the contents of `app/`).
- Implemented `RouterBridgeTransport` for real (`Bridge.provide()`/`Bridge.call()`), replacing the
  `NullTransport`-only stub. This resolves what had been the single biggest documented open item
  in the DMS-AP codebase.
- Added `app/sketch/sketch.ino` — proves the Bridge link (LED + Serial), does **not** yet touch
  FDCAN1 (see blocker #2 above).
- Switched `tensorflow` → `tflite-runtime` (with a fallback) for the TFLite interpreter — the
  right-sized package for an embedded ARM inference target.
- specs/02 §2's repository layout diagram corrected in the same change (DEV-092): removed the
  standalone `dms-rt/` top-level project that turned out not to exist as such — it's
  `dms-ap/app/sketch/`.
- 61/61 tests still pass after the move (`PYTHONPATH=app/python`).

**Unblocks:** the DMS↔VCS link finally has a real code path from Python to the MCU. **Blocks
remaining:** FDCAN1 (blocker #2), no device to test any of it on (blocker #1).

### 2026-08-10 — repo reorganized to match specs/02's own layout
**Who:** Nguyen Hoang Trieu (+ Claude)
- Renamed `drowsyguard_vcs/` → `vcs-mcxn947/` (directory, CMake project, and `.elf` name) to match
  the name specs/02 §2 actually specifies.
- Found a stray top-level `Code_prototype_python/` folder (a second, independent DMS prototype —
  MediaPipe Face Mesh with real EAR/MAR/head-pose) and filed it at
  `dms-ap/prototypes/face_mesh_ear_mar/` with a comparison against the integrated backend, instead
  of leaving it scattered.
- Created `shared/icd/` (`icd.yaml`, `crc_vectors.csv`) as the documented canonical ICD source —
  `generate.py` still doesn't exist, so the C and Python implementations remain hand-synced, but
  now against one written-down reference instead of two READMEs describing each other.
- Moved `dms-ap/tools/replay_corpus.py` → top-level `tools/replay_corpus.py` per spec.
- Added `tests/README.md` and `docs/benchmarks/README.md` stubs explaining why they're
  intentionally empty right now and what needs to exist to fill them.

### 2026-08-10 — VCS firmware: missing debug-console pin mux found and fixed
**Who:** Nguyen Hoang Trieu (+ Claude)
- Bug report: no serial log output from `vcs-mcxn947` at all after flashing.
- Root cause: `pin_mux.c` clocked and attached LPUART4 (the debug console) but never muxed the
  physical PORT1_8/PORT1_9 pins to the UART function (ALT2) — every other project in the
  NPX_Workspace toolchain does this, this one silently didn't.
- Fixed; rebuilds clean. **Anyone who had already flashed the board needs to reflash** — the
  previously-flashed image has the silent-logs bug baked in.

### 2026-08-10 — DMS-AP domain/fusion logic + VCS firmware written against the spec set
**Who:** Nguyen Hoang Trieu (+ Claude)
- `vcs-mcxn947/` (FRDM-MCXN947, MCUXpresso SDK): CAN0 link, vehicle state machine, motor drive +
  safe-stop, alert pattern engine. Builds clean, 0 warnings under `-Werror`. CAN0 pin mapping
  (PORT1_10/11, on-board TJA1057 transceiver) cross-checked against the board schematic and an SDK
  reference example — not guessed.
- `dms-ap/` (Python, first pass): D1/D2/D3 domain state machines + L0-L3 fusion ladder, all pure
  and unit-tested against the spec's own DOM-*/FUS-* requirement IDs. CAN ICD codec (`icd.py`,
  `crc8.py`) byte-identical to the C side. Camera pipeline refactored from the
  `Custom_Blaze_Face+CNN/demo.py` prototype (superseded, removed from the repo).

### 2026-08-10 — engineering spec set written (`specs/`)
**Who:** Nguyen Hoang Trieu (+ Claude)
- Full spec set (README + 01 system requirements, 02 dev standards, 03 drowsiness domain spec, 04
  CAN ICD, 05 vehicle control, 06 test plan, 07 test cases, 08 benchmark log), in English, against
  a two-node architecture: DMS (Arduino UNO Q) decides driver state, VCS (FRDM-MCXN947) decides
  vehicle response, linked by 500 kbit/s classical CAN.
- Every domain threshold (D1 distraction, D2 yawn, D3 eye closure) carries a stated rationale
  tracing to the literature it's drawn from — see spec 03.
- Written entirely pre-hardware: every figure in Rev 0.1 was a budget or a target, explicitly
  labelled as such, not a measurement.
