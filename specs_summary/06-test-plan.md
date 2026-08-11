# Summary of 06 — Test Plan

Source: [specs/06-test-plan.md](../../specs/06-test-plan.md)

## Objective
**Not** to prove DrowsyGuard works, but to find out **where it stops working** before a judge
does. Three principles:
1. Every claim needs a **number and a method** (never "detects microsleeps reliably" — instead
   "TPR 0.96 (48/50 events), 0.7 false alarms/hour, build X, run Y").
2. Measure **accuracy** and **latency** separately — merging them hides whichever one is worse.
3. **Failure paths are tested at least as hard as success paths** — cable pulled, camera
   covered, battery sagging, Linux killed — a build that hasn't passed these isn't demo-ready.

## 5 test levels
| Level | Name | Scope | Where it runs | Gate |
|---|---|---|---|---|
| L1 | Unit | One module, no hardware | CI, every PR | Merge |
| L2 | Corpus replay | Domain+fusion logic vs. annotated recordings | CI, every PR | Merge |
| L3 | Node integration | One node, real hardware, stimulated inputs | Bench, on demand | Daily |
| L4 | System integration | Both nodes, real CAN, real motors | Bench rig | Daily |
| L5 | Acceptance | Full scripted scenarios, end to end | Demo rig | Once, before demo |

**L2 is the highest-value test in the project** — because `domains/`/`fusion/` are pure
functions (DEV-042), a 30-minute annotated recording replays through the exact production
decision logic in under a second, deterministically, with no camera and no board. This is the
mechanism that stops "tuned it until the demo worked" from silently destroying the false-alarm rate.

## Bench rig
- An 8-channel logic analyser measures every timing point (inference done, CAN TX/RX,
  actuator change, 100Hz control tick, motor enable, CAN_H/L decode).
- **Latency is measured with the logic analyser, not software timestamps across nodes** — two
  unsynchronised clocks can't measure a cross-node interval, the scope can.
- GPIO markers must be compiled into both debug **and** release builds (overhead measured once, recorded).
- All motion testing on a **wheel stand** until all TC-SAF cases pass — a safe-stop bug on the
  floor is a vehicle that drives into something.

## Stimulus methods
| Method | Used for | Repeatable? |
|---|---|---|
| Corpus replay into the pipeline | Domain/fusion/threshold logic | Fully deterministic |
| Monitor playback to the camera | Full optical path (exposure, IR, glare) | Repeatable within lighting tolerance |
| Live human subject, scripted actions | UX/realism check, acceptance | **Not repeatable — never used to produce a quoted metric** |
| CAN frame injection (`can_inject.py`) | All VCS behaviour, no camera needed | Fully deterministic |

## Test corpora (3 sets, version-controlled via git-lfs)
| Corpus | Content | Target duration | Purpose |
|---|---|---|---|
| **C-BASE** | Alert subjects, normal behaviour (blinking, talking, mirror checks, drinking...) | ≥60 min | **False-alarm measurement** — decides if the product is usable |
| **C-DROWSY** | Acted/genuine drowsiness (long closures, microsleeps, yawns, head nods) | ≥30 min | TPR measurement |
| **C-ADVERSE** | Darkness+IR, glare, glasses (clear/sunglasses), face occluded, camera shake | ≥20 min | Degradation/fault behaviour |

- Split **by subject** (not by clip) into tuning and held-out sets — otherwise tuning and
  evaluating on the same faces produces a meaningless number.
- Ambiguous events (a yawn vs. just a deep breath?) get **two independent annotators**,
  disagreement rate reported — a detector can't be more accurate than its ground truth.

## Acceptance criteria — the core table
| # | Criterion | Target |
|---|---|---|
| AC-01 | Microsleep TPR (closure≥1.5s) | ≥ 0.95 |
| AC-02 | False alarms L1+ on C-BASE | ≤ 1.0/hour |
| AC-03 | Yawn F1 | ≥ 0.85 |
| AC-04 | Distraction TPR | ≥ 0.90 |
| AC-05 | Pipeline latency P95 | ≤ 200ms |
| AC-06 | Sustained FPS | ≥ 8 |
| AC-07 | FPS at min 30 vs. min 1 | ≥ 80% |
| AC-08 | Control-loop jitter | ≤ ±1ms |
| AC-09/10 | `LINK_LOST`/safe-stop timing | 300ms / 1000ms (tight tolerance) |
| AC-11 | Safe-stop duration | 2.0s±0.1s |
| AC-12/13 | Unexpected states / bus-off in 30min | 0 |
| AC-14 | Open ⚠️ ASSUMPTION items | 0 |
| AC-15 | Sound pressure | ≤85dB(A) |

- If a criterion isn't met: **report the real number** in the demo, don't adjust the criterion —
  "our P95 is 240ms, above our 200ms target, and here's why" is more credible than quoting a
  target as if it were a measurement.

## Entry/Exit criteria (practical checklists before touching the real bench)
- **Entry to L4:** both nodes build clean from main, CAN bring-up checklist complete, CRC
  vectors pass, chassis on a wheel stand.
- **Exit L4 / entry L5:** all TC-CAN/TC-SAF pass, 30-minute run with no unexpected state or
  bus-off, all benchmark entries in spec 08 filled with real numbers, open-items registers empty.
- **Exit L5 (demo-ready):** all acceptance criteria met (or each miss documented with its actual
  number), demo rehearsed twice with no operator intervention, **a recorded backup video** exists
  in case demo hardware fails.

## Regression policy
- Any defect found at L3+ results in a new test case at the **lowest** level that could have
  caught it — a bug found on the bench that a corpus replay could have caught means an L2 gap,
  and that gap is the real defect.
- The L1+L2 suite must run **under 3 minutes** — a slower suite gets skipped under deadline pressure.
- The author of a module is **not** the only person testing it at L4 (avoids the same mental
  model that caused the bug also missing it in testing).
