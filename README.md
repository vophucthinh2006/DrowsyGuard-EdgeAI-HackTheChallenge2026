# DrowsyGuard-EdgeAI-HackTheChallenge2026
> **Edge-AI Driver Drowsiness Detection System on Arduino UNO Q**  
> *A project built by MLIoT_Love50 Team (HCMUT EE Machine Learning & IoT Lab) for **Qualcomm FutureMakers: Hack The Challenge 2026***

---

## About The Competition
This project was developed for **Qualcomm FutureMakers: Hack The Challenge 2026**, organized by Qualcomm. The competition focuses on leveraging Qualcomm technologies and the **Arduino UNO Q** platform to solve real-world problems through deployable, scalable, and highly applicable solutions.

**DrowsyGuard** addresses key challenge tracks defined by the competition:
* 🔒 **Security & Privacy**: 100% on-device processing guarantees driver privacy by ensuring zero camera frames leave the device.
* 🚘 **Smart Living (Safety)**: Real-time driver monitoring to prevent fatigue-related road accidents.
* ⚡ **Edge–Cloud AI / Edge AI**: Real-time vision inferencing on Qualcomm hardware with optional cloud telemetry logging.

---

## Project Overview
DrowsyGuard is an offline, privacy-preserving, and low-latency Edge-AI driver monitoring solution. Operating entirely on-device, it utilizes a dual-brain architecture (Linux + MCU) on the **Arduino UNO Q** board to process computer vision models and trigger multi-stage physical interventions before micro-sleep leads to dangerous accidents.

---

## System Architecture
The system runs on the **Arduino UNO Q** platform utilizing two distinct processing layers:
* **Qualcomm Dragonwing QRB2210 (Linux Core)**: Runs MediaPipe face-landmark vision pipelines to track eyes (PERCLOS), mouth (yawn rate), and head movements (nodding) to compute real-time drowsiness scores (Level 0–3).
* **STM32U585 (Microcontroller Core)**: Executes ultra-low latency safety actions (<100ms), triggering buzzers, haptic feedback, fan relays, and status LEDs.

---

## Key Features
* **100% On-Device Processing**: High privacy preservation with zero network dependency for core AI inference.
* **Day & Night Vision**: Integrates dual RGB and Infrared (IR) camera streams for reliable cabin monitoring under all light conditions.
* **Escalating Alert System**:
  * **Level 0 (Normal)**: Silent monitoring.
  * **Level 1 (Early Warning)**: Subtle audio beeps and amber LED indicator.
  * **Level 2 (Drowsy)**: High-decibel alarm, haptic seat vibration, and cooling fan relay.
  * **Level 3 (Danger)**: Emergency hazards, slowdown signals, and SMS/GPS telemetry logging.

---

## Tech Stack & Hardware
* **Hardware**: Arduino UNO Q (Qualcomm QRB2210 + STM32U585), RGB/IR Camera Modules, Vibration Motors, Cooling Fan Relay, Status LEDs, Buzzer.
* **AI & Software**: MediaPipe, FLOAT32 - INT8 Quantization, Python, C/C++, Qualcomm AI Hub.

---

## Repository layout

Follows [specs/02-development-standards.md §2](specs/02-development-standards.md#2-repository-layout)
exactly — that document is the authoritative layout; this table is a map onto the current state of
it, including what's genuinely still missing.

| Path | What | Status |
|---|---|---|
| [`specs/`](specs/README.md) | Full engineering spec set — system requirements, dev standards, drowsiness domain spec, CAN ICD, vehicle control, test plan/cases, benchmark log | Complete |
| [`dms-ap/`](dms-ap/README.md) | Driver Monitoring System — an **Arduino App Lab app** (`app/python/` on the QRB2210/Linux MPU, `app/sketch/` on the STM32U585 MCU — "DMS-RT" lives here, not as a separate project, see below) | 61/61 unit tests pass; camera pipeline + Bridge link unexercised on real hardware, see its README |
| [`vcs-mcxn947/`](vcs-mcxn947/README.md) | Vehicle Control Simulator firmware (FRDM-MCXN947) — CAN link, motor drive, safe-stop, physical alerts | Builds clean; not yet flashed/measured on hardware |
| [`shared/icd/`](shared/icd/README.md) | The canonical DMS↔VCS CAN interface — `icd.yaml`, `crc_vectors.csv` | Source-of-truth files exist; the C/Python implementations are still hand-synced, not generated (`generate.py` doesn't exist yet) |
| [`tools/`](tools/replay_corpus.py) | Cross-cutting scripts — corpus replay today, flashers/`can_inject.py` in the future | `replay_corpus.py` only so far |
| [`tests/`](tests/README.md) | Cross-node integration + HIL tests | Empty — needs `tools/can_inject.py` or real two-node hardware first |
| [`docs/benchmarks/`](docs/benchmarks/README.md) | Raw measurement artefacts backing every number in [specs/08](specs/08-benchmark-log.md) | Empty — 0/15 acceptance criteria measured so far |

**`dms-ap/` runs on the UNO Q via Arduino App Lab.** Its AP↔RT link (the biggest open item as of
the previous revision of this table) is now a real implementation on **Arduino_RouterBridge**
(MessagePack RPC between the Python/MPU side and the sketch/MCU side, confirmed against a real
published App Lab app, not guessed — see `dms-ap/README.md` "App Lab" section). What's still open:
the sketch's FDCAN1 relay to the physical DMS↔VCS bus (`dms-ap/app/sketch/sketch.ino`'s FDCAN
section — there's an open, unresolved Arduino forum thread on exactly this question as of this
writing) and running any of it on a real device (none available in this environment).

`vcs-mcxn947/` builds against an MCUXpresso SDK toolchain that lives outside this repo (see that
project's README) — only its source is here.

---

## MLIoT_Love50 Team (Faculty of Electrical & Electronics Engineering - HCMUT EE Machine Learning & IoT Lab)
* **Nguyen Hoang Trieu** – Team Lead & Embedded System
* **Tang Phon Thinh** – AI & Computer Vision
* **Van Dac Phong Truc** – Software & Integration
* **Vo Phuc Thinh** – Connectivity & Cloud Integration
