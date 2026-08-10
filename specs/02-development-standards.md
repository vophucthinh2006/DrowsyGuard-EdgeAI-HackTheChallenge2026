# 02 — Development & Deployment Standards

**Document:** DG-SPEC-02 · Rev 0.1 · 2026-08-10 · DRAFT
**Applies to:** every person and every commit on the DrowsyGuard repository

---

## 1. Why this document exists

Four people writing firmware for two different MCUs and a Linux application, in six days, against
hardware that arrives mid-project. The failure mode is not "we cannot write the code" — it is
"the three parts do not fit together, and nobody can tell which one is wrong."

Every rule below exists to make a specific failure mode impossible or immediately visible. Where a
rule is inconvenient, its rationale is stated so you can judge whether the inconvenience is worth
it in your case. Rules marked **SHALL** are not negotiable without a change request (§11).

---

## 2. Repository layout

**DEV-001** — The project SHALL be a single monorepo. One commit SHALL be able to change the CAN
layout on both sides at once.

```
drowsyguard/
├── specs/                     # this specification set — the contract
├── dms-ap/                    # QRB2210, Linux, Python 3.11 — an Arduino App Lab app
│   ├── app/                     # THE deployable unit — everything App Lab syncs to the device
│   │   ├── app.yaml               # App Lab manifest (name, version, bricks, ports)
│   │   ├── python/                 # runs on the MPU (QRB2210/Linux)
│   │   │   ├── main.py               # App Lab entry point (App.run(user_loop=...))
│   │   │   ├── requirements.txt
│   │   │   ├── config/thresholds.yaml # SINGLE source of every tunable number
│   │   │   ├── models/                # .tflite — deploys with the app
│   │   │   └── drowsyguard/            # the actual package
│   │   │       ├── capture/           # camera abstraction
│   │   │       ├── inference/         # model runner (backend-swappable)
│   │   │       ├── domains/           # d1_distraction.py d2_yawn.py d3_eyeclosure.py
│   │   │       ├── fusion/            # alert ladder state machine
│   │   │       ├── link/              # AP↔RT transport (RouterBridge) + CAN ICD codec
│   │   │       └── telemetry/         # logging, metrics
│   │   └── sketch/                  # runs on the MCU (STM32U585) — this IS "DMS-RT",
│   │                                   not a separate top-level project (see below)
│   │       ├── sketch.ino
│   │       └── sketch.yaml           # Arduino CLI profile, auto-managed
│   ├── prototypes/             # reference material, not deployed, not imported by app/
│   ├── tests/                  # unit tests, PYTHONPATH=app/python
│   └── pyproject.toml          # local dev/test packaging only — not what ships to the device
├── vcs-mcxn947/               # FRDM-MCXN947 firmware (MCUXpresso SDK, out-of-tree)
│   ├── src/
│   │   ├── can_rx.c           # ICD decode + timeout supervision
│   │   ├── motion.c           # differential drive, ramps, safe stop
│   │   ├── alerts.c           # buzzer / LED / haptics
│   │   └── safety.c           # failsafe state machine, watchdog
│   ├── inc/dg_config.h        # generated — DO NOT EDIT BY HAND
│   ├── prj.conf  CMakeLists.txt  build.sh
├── shared/
│   ├── icd/drowsyguard.dbc    # CAN database — the single interface truth
│   ├── icd/icd.yaml           # machine-readable ICD source
│   └── icd/generate.py        # emits C header + Python module + .dbc
├── tools/                     # flashers, corpus annotator, bench scripts
├── tests/                     # cross-node integration + HIL
└── docs/benchmarks/           # raw measurement artefacts referenced by spec 08
```

**Revision note (Rev 0.2):** the original version of this layout put DMS-RT at a separate
top-level `dms-rt/` (implying its own build.sh, mirroring `vcs-mcxn947/`). That assumption did not
survive contact with the actual Arduino UNO Q platform: App Lab requires one app folder
(`dms-ap/app/`) containing *both* the Python (MPU) and the sketch (MCU) halves together, tied by
one `app.yaml`, deployed as one unit — there is no separate top-level firmware project for the
UNO Q's own microcontroller the way there is for the external `vcs-mcxn947/` board. `dms-ap/app/sketch/`
**is** DMS-RT. Likewise `dms-ap/src/` became `dms-ap/app/python/` (App Lab's required folder
name, not a free choice), and `config/`/`models/` moved inside `app/python/` because App Lab only
deploys the contents of `app/` to the device — anything DMS-AP needs at runtime has to live there,
not as a sibling directory. See `dms-ap/README.md` "App Lab" section for the verification trail
behind this structure (it was confirmed against a real published App Lab app on GitHub, not
guessed) and DEV-092: this is exactly "reality disagreed with the spec, update the spec in the
same change" applied to this document itself.

**DEV-002** — `shared/icd/icd.yaml` SHALL be the only place message layouts are written. The C
header, the Python module and the `.dbc` SHALL be **generated** from it by `generate.py`.
Hand-editing a generated file is a defect.
*Rationale: the single most common two-node integration bug is a byte-layout disagreement. If both
sides cannot disagree, the bug cannot exist.*

**DEV-003** — Generated artefacts SHALL be committed (not gitignored), and CI SHALL fail if
re-running the generator produces a diff. This makes drift a red build rather than a mystery on the
bench.

---

## 3. Branching, commits, review

**DEV-010** — Trunk-based development. `main` SHALL always build and SHALL always be flashable.
Branches SHALL be short-lived (< 1 day) and named `feat/<area>-<slug>`, `fix/<area>-<slug>`,
`spec/<slug>`, `test/<slug>`.

**DEV-011** — Commit messages SHALL follow Conventional Commits with the node as scope:

```
<type>(<scope>): <imperative summary, ≤ 72 chars>

<why this change is necessary — not what the diff shows>

Refs: SYS-FR-012, DOM-D3-004
```

`type` ∈ `feat | fix | perf | refactor | test | docs | build | chore`
`scope` ∈ `ap | rt | vcs | icd | specs | tools | ci`

**DEV-012** — Any commit that changes behaviour covered by a requirement SHALL cite the
requirement ID in a `Refs:` trailer. This is what makes the traceability matrix in
[01 §9](01-system-requirements.md#9-verification-matrix) maintainable rather than aspirational.

**DEV-013** — Pull requests SHALL be squash-merged. Minimum **one** approving review.

**DEV-014** — Changes touching any of the following SHALL require **two** approvals, one of whom
did not write the code:
- `vcs-mcxn947/src/safety.c`, `can_rx.c` (failsafe and timeout paths)
- `dms-ap/app/python/drowsyguard/fusion/` (the alert ladder)
- `shared/icd/` (the interface)
- any threshold in `config/thresholds.yaml`

*Rationale: these four are the places where a plausible-looking change silently disables a safety
behaviour without breaking a single test that anyone remembers to run.*

**DEV-015** — Never commit directly to `main`, never force-push `main`, never merge a red build.

---

## 4. Coding standards — firmware (C)

**DEV-020** — Language: **C11**. Compiler flags SHALL include:
`-Wall -Wextra -Werror -Wshadow -Wconversion -Wundef -fno-common -ffunction-sections -fdata-sections`

**DEV-021** — After initialisation completes, firmware SHALL perform **no dynamic memory
allocation**. No `malloc`, no `new`, no variable-length arrays. All buffers static and sized at
compile time.

**DEV-022** — Fixed-width types only in every interface and every struct: `uint8_t`, `int16_t`,
`uint32_t`. Bare `int`, `long`, `char` (as a number) SHALL NOT appear in any function signature or
struct field. `bool` from `<stdbool.h>` is permitted.

**DEV-023** — Naming:

| Kind | Convention | Example |
|---|---|---|
| Module-public function | `<module>_<verb>_<noun>` | `motion_set_duty()` |
| Module-private function | `static` + same form | `static motion_clamp_duty()` |
| Type | `<module>_<noun>_t` | `motion_state_t` |
| Enum constant | `<MODULE>_<NOUN>_<VALUE>` | `MOTION_STATE_DECEL` |
| Macro / constant | `DG_UPPER_SNAKE` | `DG_CAN_TIMEOUT_MS` |
| File-scope variable | `s_` prefix | `static uint32_t s_tick_count;` |
| Global variable | forbidden without a written exception | |

**DEV-024** — Every `.c` file SHALL have exactly one matching `.h` exposing only what other
modules need. Anything not in the header SHALL be `static`.

**DEV-025** — Interrupt service routines SHALL:
- execute in **≤ 50 µs** worst case;
- contain no `printf`/`PRINTF`, no blocking wait, no floating point, no allocation;
- do nothing but capture data and post to a queue or set a flag;
- be named `<PERIPH>_IRQHandler` and live in the owning module.

**DEV-026** — No magic numbers. Every timing constant, threshold and limit SHALL be a named
constant in `dg_config.h`, which is **generated** from `config/thresholds.yaml` so the MCU and the
Linux side cannot hold different values for the same threshold.

**DEV-027** — Every state machine SHALL be implemented as an explicit `switch` over an enum with a
`default:` that logs and enters the safe state. Implicit state held in scattered booleans is a
defect.

**DEV-028** — MISRA-C:2012 is used as a **guideline, selectively enforced**. The enforced subset is
listed in `vcs-mcxn947/misra-subset.md` and checked by `cppcheck --addon=misra`. Claiming full MISRA
compliance is forbidden — we do not have the tooling to substantiate it.

**DEV-029** — Return values of functions that can fail SHALL be checked. Deliberately ignoring one
SHALL be written as `(void)fn();` with a comment saying why.

**DEV-030** — Floating point SHALL NOT be used in the VCS control loop. All duty, ramp and timing
arithmetic SHALL be integer or fixed-point (Q16.16 where fractions are needed).
*Rationale: deterministic timing and no surprise FPU-context cost in an ISR path.*

### 4.1 Platform-specific rules learned the hard way

**DEV-031** — On MCXN947, SDK drivers are opt-in. A missing `CONFIG_MCUX_COMPONENT_driver.<x>=y`
in `prj.conf` produces an **undefined reference at link time**, not a compile error. When a symbol
is missing, check `prj.conf` before you check your code.

**DEV-032** — On MCXN947, all peripheral clock gating is manual (`CLOCK_EnableClock`). A peripheral
with no clock does not error — it silently does nothing and reads back zeros. Every peripheral
bring-up SHALL start by confirming the clock gate and reset release.

**DEV-033** — `debug_console_lite`'s `PRINTF` does not parse the `l` length modifier. `%lu` prints
the literal characters `lu`. Use `%u` with an `(unsigned int)` cast. Do not assume a successful
build means correct output — read the serial log.

**DEV-034** — Out-of-tree MCUXpresso apps: `PROJECT_BOARD_PORT_PATH` SHALL be a **relative** path
(resolved against `${SdkRootDirPath}`). An absolute path silently resolves wrong and `pin_mux.c` is
not found.

---

## 5. Coding standards — application (Python)

**DEV-040** — Python **3.11**. Formatting `black` (line length 100), linting `ruff` with the ruleset
in `pyproject.toml`. Both run in CI and in the pre-commit hook.

**DEV-041** — Type hints SHALL be present on every public function signature. `mypy --strict` on
`app/python/drowsyguard/domains/` and `app/python/drowsyguard/fusion/` — the two packages where a type confusion
becomes a wrong alert.

**DEV-042** — The fusion and domain modules SHALL be **pure**: they take timestamped observations
in and return state out, with no I/O, no camera handle, no clock read. Time SHALL be passed in as a
parameter.
*Rationale: this is what makes it possible to replay a 30-minute annotated corpus through the exact
production decision logic in under a second, deterministically, in CI. If the fusion code calls
`time.time()` internally, that entire class of testing is gone.*

**DEV-043** — No thresholds in code. `config/thresholds.yaml` is loaded once at startup and passed
in. A hard-coded `0.8` anywhere in `domains/` is a defect.

**DEV-044** — The inference backend SHALL sit behind an interface (`InferenceBackend`) with at
least two implementations: the real on-device runtime and a `ReplayBackend` that reads
pre-computed detections from a corpus file. Everything above the backend SHALL be testable with no
camera and no accelerator.

**DEV-045** — Long-running loops SHALL be structured so a `SIGTERM` results in a clean shutdown
that publishes `level = L0`, `calib_done = 0` and disarms the VCS. Dying without telling the
vehicle is a safety defect, not an inconvenience.

---

## 6. Configuration and the threshold single-source rule

**DEV-050** — `dms-ap/config/thresholds.yaml` is the **single source of truth** for every number in
[03 — Drowsiness Domain Specification](03-drowsiness-domain-spec.md).

**DEV-051** — CI SHALL run `tools/check_thresholds.py`, which parses the normative tables out of
`specs/03-drowsiness-domain-spec.md` and asserts they match `thresholds.yaml`. A mismatch fails the
build.
*Rationale: specifications rot the moment the code disagrees with them and nobody notices. Making
the disagreement a build failure is the only mechanism that actually works under time pressure.*

**DEV-052** — Every threshold entry SHALL carry a `rationale:` field. A number with no reason
behind it cannot be defended to a judge and cannot be safely changed by a teammate.

```yaml
d3_eye_closure:
  active_dwell_ms:
    value: 800
    rationale: >
      ~2x the upper bound of normal blink duration (100-400 ms), placing the
      threshold outside the normal blink distribution. See spec 03 section 5.2.
    verified_against: null        # set to a benchmark run ID once measured
```

---

## 7. Logging

**DEV-060** — One line per event, key=value, machine-parseable, in this form:

```
<ISO8601-UTC> <LEVEL> <module> event=<name> k1=v1 k2=v2
2026-08-14T09:31:02.417Z WARN fusion event=level_change from=L1 to=L2 \
  cause=d3_severe perclos=0.17 closure_ms=1520 face_conf=0.91 seq=44
```

**DEV-061** — Levels: `ERROR` (system cannot continue correctly), `WARN` (degraded or safety-relevant),
`INFO` (state transitions, once per event), `DEBUG` (off in release builds).

**DEV-062** — Logs SHALL NOT contain images, landmark coordinates, or anything from which a face
could be reconstructed. This is a hard requirement flowing from
[SYS-AR-004](01-system-requirements.md#3-system-architecture-requirements), and it applies to debug
builds too.

**DEV-063** — On the MCU side, logging SHALL NOT be performed from an ISR (see DEV-025) and SHALL
be rate-limited so that a fault storm cannot stall the control loop.

**DEV-064** — Every benchmark run SHALL be logged to `docs/benchmarks/<run-id>/` containing: the
raw log, the git SHA, the SHA-256 of `thresholds.yaml`, and the environment record. Numbers quoted
in [08](08-benchmark-log.md) without a matching artefact directory are not results.

---

## 8. Build

**DEV-070** — Each node SHALL provide a `build.sh` that builds from clean with no arguments and
exits non-zero on any failure. No IDE SHALL be required to produce a releasable artefact.

**DEV-071** — Firmware SHALL embed and print at boot: semantic version, git short SHA, `+dirty`
suffix if the tree was not clean, and build timestamp.

```
DrowsyGuard VCS v0.3.1 (a1b2c3d+dirty) built 2026-08-14T08:02:11Z
```

*Rationale: on a bench with three boards and six flash cycles an hour, "which build is on that
board" is the question that wastes the most time. The board should answer it.*

**DEV-072** — A `+dirty` build SHALL NOT be used to produce any number recorded in
[08](08-benchmark-log.md).

**DEV-073** — CI (on every PR) SHALL run, in order: ICD regeneration diff check → threshold
consistency check → `ruff` + `black --check` + `mypy` → Python unit tests → corpus replay
regression → both firmware builds → `cppcheck` + `clang-tidy`. Any failure blocks merge.

---

## 9. Flashing and deployment

**DEV-080** — Flashing SHALL be scripted, never a sequence of remembered GUI clicks.

### 9.1 VCS — FRDM-MCXN947

```bash
# probe UID is pinned: with an ST-Link also attached, pyOCD prompts for a probe
# choice and any script dies with "EOF when reading a line".
PROBE_UID=$(pyocd json --probes | python3 -c \
  "import json,sys; print(next(p['unique_id'] for p in json.load(sys.stdin)['boards'] \
   if 'MCU-LINK' in p['board_name'].upper()))")
pyocd flash -u "$PROBE_UID" -t mcxn947 build/vcs.hex
```

**DEV-081** — The MCU-Link probe SHALL be reachable without `sudo` via the udev rule
`/etc/udev/rules.d/50-cmsis-dap.rules` (vendor `1fc9`, `MODE="0660"`, `TAG+="uaccess"`). If a
`sudo` fallback is ever used it SHALL carry `env HOME=$HOME` — the CMSIS pack lives in the user's
home directory and plain `sudo` looks in `/root`.

### 9.2 DMS

**DEV-082** — DMS-RT (STM32U585) SHALL be flashed with the Arduino Flasher CLI / `arduino-cli`
invocation recorded in `dms-rt/build.sh`. DMS-AP SHALL be deployed by an idempotent
`tools/deploy_ap.sh` that pushes the wheel + model + config and restarts the service — never by
`scp`-ing individual files.

**DEV-083** — Every deployment SHALL end by reading back the boot banner and asserting the SHA
matches what was intended. A deploy that is not verified did not happen.

**DEV-084** — Model weights SHALL be versioned separately from firmware (`models/<name>-vX.Y.tflite`)
and the active model name + SHA-256 SHALL appear in the boot log and be retrievable over CAN
diagnostics.

---

## 10. Definition of Done

A task is done when **all** of the following are true. Not "mostly".

- [ ] Code merged to `main` via reviewed PR; CI green.
- [ ] Requirement IDs cited in the commit trailer.
- [ ] Unit tests added for new logic; corpus replay regression still passes.
- [ ] Any new tunable is in `thresholds.yaml` with a `rationale:` field.
- [ ] Any interface change went through `icd.yaml` and regenerated both sides.
- [ ] **Verified on real hardware**, not just in simulation — with the observation written down.
- [ ] If it changes a number quoted in a spec, the spec is updated in the same PR.
- [ ] If it produced a measurement, the run is recorded in [08](08-benchmark-log.md) with its
      artefact directory.
- [ ] No new ⚠️ ASSUMPTION introduced without being added to an Open Items register.

---

## 11. Change control

**DEV-090** — Specification documents change only by PR, with the revision history table updated in
the same commit.

**DEV-091** — A change to a **normative threshold** in [03](03-drowsiness-domain-spec.md) SHALL
include, in the PR description:
1. the old value and the new value;
2. the evidence that motivated the change (benchmark run ID or corpus result);
3. the effect on the false-alarm rate measured on the baseline corpus;
4. two approvals per DEV-014.

*Rationale: thresholds are where a demo gets "tuned" until it passes, and where the honest number
quietly disappears. Requiring the false-alarm impact makes the trade-off visible instead of silent.*

**DEV-092** — When measured reality contradicts a spec, the spec is updated to reality **in the same
pull request** as the discovery. Do not leave a known-false number in a document. Do not quietly
"fix" the measurement to match the document.

---

## 12. Anti-patterns — explicitly forbidden

| Anti-pattern | Why it is banned here |
|---|---|
| Tuning a threshold until the live demo happens to work | Overfits to one person, one light, one day. Change it against the corpus with §11 evidence, or not at all. |
| `sleep()` in the VCS control loop | Destroys loop determinism and hides timing bugs until they surface as jitter at the demo. |
| Catching a broad `except Exception:` and continuing | Turns a detector crash into "the driver is fine". Failures SHALL surface as a fault state. |
| Commenting out a failing test to unblock a merge | The test was the only thing that knew about the bug. Fix or explicitly quarantine with an issue link. |
| Committing a measured number without its artefact directory | An unreproducible number is a claim, not a result. |
| Demonstrating with a `+dirty` build | Nobody can reproduce what was shown. |
| Adding a second copy of a threshold "just for now" | The two copies will diverge, and the bug will look like a model problem. |

---

## Revision history

| Rev | Date | Author | Change |
|---|---|---|---|
| 0.1 | 2026-08-10 | ML_IoT_Love50 | Initial baseline |
