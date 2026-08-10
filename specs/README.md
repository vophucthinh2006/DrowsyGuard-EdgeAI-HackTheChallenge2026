# DrowsyGuard — Engineering Specification Set

**Project:** DrowsyGuard — on-device driver drowsiness detection with vehicle-side intervention
**Team:** ML_IoT_Love50
**Event:** Qualcomm Future Makers — Hack The Challenge 2026
**Baseline:** Rev 0.1 · 2026-08-10 · **DRAFT — pre-hardware**

---

## 1. What this set covers

DrowsyGuard is a two-node embedded system:

| Node | Hardware | Role |
|---|---|---|
| **DMS** — Driver Monitoring System | Arduino UNO Q (Qualcomm Dragonwing QRB2210 + STM32U585) | Camera capture, MediaPipe face-landmark inference, drowsiness fusion, alert ladder, connectivity |
| **VCS** — Vehicle Control Simulator | NXP FRDM-MCXN947 + 4× TT gear motor + H-bridge driver | Vehicle dynamics simulation, speed limiting, safe-stop, physical alert actuators |

The two nodes are joined by a **500 kbit/s classical CAN bus**. The DMS decides *how drowsy the
driver is*; the VCS decides *what the vehicle does about it*. That split is deliberate: it keeps
the safety-critical actuation path on a deterministic MCU that never waits on Linux, and it makes
the drowsiness decision independently testable from the vehicle behaviour.

## 2. Document map — read in this order

| # | Document | Answers |
|---|---|---|
| 01 | [System Requirements Specification](01-system-requirements.md) | What the system must do, how fast, how safely, under what conditions |
| 02 | [Development & Deployment Standards](02-development-standards.md) | How the team writes, reviews, builds, flashes and releases code |
| 03 | [Drowsiness Domain Specification](03-drowsiness-domain-spec.md) | The three detection domains, every threshold, every dwell time, and *why* each number is that number |
| 04 | [Interface Control Document — CAN](04-interface-control-document.md) | Byte-exact CAN message layouts, timing, timeout behaviour |
| 05 | [Vehicle Control Specification](05-vehicle-control-spec.md) | Motor drive, alert actuators, speed limiting, safe-stop profile, failsafe |
| 06 | [Test Plan](06-test-plan.md) | Test strategy, rig setup, measurement method, entry/exit criteria |
| 07 | [Test Case Catalogue](07-test-cases.md) | Every executable test case with its pass criterion |
| 08 | [Benchmark Log](08-benchmark-log.md) | Where measured numbers get recorded, with the exact procedure that produced them |

## 3. Identifier conventions

Every normative statement carries an ID so tests and code can reference it.

| Prefix | Meaning | Example |
|---|---|---|
| `SYS-FR-nnn` | System functional requirement | `SYS-FR-012` |
| `SYS-PR-nnn` | System performance requirement | `SYS-PR-003` |
| `SYS-SR-nnn` | System safety requirement | `SYS-SR-005` |
| `SYS-IR-nnn` | System interface requirement | `SYS-IR-002` |
| `SYS-ER-nnn` | Environmental / operational requirement | `SYS-ER-001` |
| `DOM-Dx-nnn` | Detection-domain requirement | `DOM-D3-004` |
| `VEH-nnn` | Vehicle control requirement | `VEH-018` |
| `CAN-nnn` | Interface requirement | `CAN-007` |
| `DEV-nnn` | Development standard rule | `DEV-021` |
| `TC-AREA-nnn` | Test case | `TC-DOM-014` |

Requirement keywords follow **RFC 2119**: **SHALL** = mandatory, **SHOULD** = recommended
(deviation must be recorded), **MAY** = optional.

## 4. Status legend used throughout

| Tag | Meaning |
|---|---|
| ✅ **VERIFIED** | Measured on real hardware; the measurement is logged in [08](08-benchmark-log.md) |
| 🟡 **DESIGNED** | Specified and implemented, not yet measured |
| ⬜ **PLANNED** | Specified only, no implementation |
| ⚠️ **ASSUMPTION** | Stated as fact in the design but **not yet confirmed against a datasheet or hardware** — must be closed before it is relied upon |

Every ⚠️ item is also listed in the Open Items register of the document it appears in. **No ⚠️
item may remain open at the acceptance-test gate.**

## 5. Known state at Rev 0.1

This revision was written **before the loaned Arduino UNO Q hardware was in hand**. It is a
design baseline, not an as-built record. Concretely:

- Every performance figure in this set is a **budget or a target**, not a measurement. There are
  currently **zero** ✅ VERIFIED entries.
- The CAN pin mapping on the **DMS (UNO Q)** side is still an ⚠️ ASSUMPTION (see
  [04 §9 Open Items](04-interface-control-document.md#9-open-items)). The **VCS (FRDM-MCXN947)**
  side is no longer an assumption: its CAN0/PWM1/WWDT pin mapping is implemented and builds clean
  in `NPX_Workspace/drowsyguard_vcs/` — see that project's README for the cross-reference trail.
  It has not been flashed or measured on real hardware yet, so it is 🟡 DESIGNED, not ✅ VERIFIED.
- All threshold values in [03](03-drowsiness-domain-spec.md) are derived from published
  literature and must be re-tuned against the team's own annotated corpus before acceptance.

Treat this as the contract the implementation is measured against, and raise a change request
(see [02 §11](02-development-standards.md#11-change-control)) when reality disagrees with it.

## 6. Change control

Specification changes follow the process in
[02 §11 Change Control](02-development-standards.md#11-change-control). Every document carries a
revision history table at its foot. The revision of this README is the revision of the set as a
whole.

| Rev | Date | Author | Change |
|---|---|---|---|
| 0.1 | 2026-08-10 | ML_IoT_Love50 | Initial baseline, pre-hardware |
| 0.2 | 2026-08-10 | ML_IoT_Love50 | VCS firmware implemented against specs 02/04/05 (`NPX_Workspace/drowsyguard_vcs/`); closes 04-OI-04-05/partial-03/04 and 05-OI-05-05 for the VCS side only |
