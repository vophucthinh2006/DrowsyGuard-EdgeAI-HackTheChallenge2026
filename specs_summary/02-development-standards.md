# Summary of 02 — Development & Deployment Standards

Source: [specs/02-development-standards.md](../../specs/02-development-standards.md)

## Why this document exists
Four people writing firmware for two different MCUs plus a Linux app, in six days, with
hardware arriving mid-project. The biggest risk isn't "we can't write the code" — it's "the
three parts don't fit together, and nobody can tell which one is wrong."

## Repository layout — the part most likely to be misunderstood
```
drowsyguard/
├── specs/                # the engineering contract
├── dms-ap/app/           # the ONE unit App Lab deploys to the UNO Q
│   ├── app.yaml
│   ├── python/           # runs on the MPU (QRB2210/Linux) — Python 3.13
│   │   ├── main.py
│   │   ├── config/thresholds.yaml   # SINGLE source of truth for every threshold
│   │   ├── models/*.tflite
│   │   └── drowsyguard/{capture,inference,domains,fusion,link,telemetry}
│   └── sketch/           # runs on the MCU STM32U585 — THIS IS "DMS-RT", not a separate project
├── vcs-mcxn947/          # FRDM-MCXN947 firmware (MCUXpresso SDK, out-of-tree)
├── shared/icd/           # icd.yaml = single source of truth for CAN layout, generates .h/.py/.dbc
├── tools/ tests/ docs/benchmarks/
```
**Key point:** there is no separate top-level `dms-rt/` project as originally assumed — App Lab
requires one `app/` folder containing both the Python (MPU) and sketch (MCU) halves, deployed as
a single unit. This is a real lesson learned (the spec corrected itself twice — see DEV-092 below).

- `icd.yaml` is the only place CAN message layouts are written; the C header, Python module,
  and `.dbc` file are all **generated** from it — hand-editing a generated file is a defect. CI
  fails if regenerating produces a diff.

## Git / review
- Trunk-based; `main` always builds and flashes. Short-lived branches (<1 day): `feat/`, `fix/`,
  `spec/`, `test/`.
- Conventional Commits + a `Refs: SYS-FR-012, DOM-D3-004` trailer to keep the traceability matrix maintainable.
- Squash-merge, at least 1 approval.
- **Two approvals required** (one from someone who didn't write the code) for changes to:
  `safety.c`/`can_rx.c` (failsafe/timeout), `fusion/` (the alert ladder), `shared/icd/`
  (the interface), any threshold in `thresholds.yaml`.
- Never commit directly to main, never force-push, never merge a red build.

## Coding standards — Firmware (C11)
- Strict compiler flags: `-Wall -Wextra -Werror -Wshadow -Wconversion ...`
- After init: **no dynamic allocation** (no malloc/new/VLA), buffers static and compile-time sized.
- Fixed-width types only (`uint8_t` etc.) in every interface/struct.
- Clear naming conventions (module_verb_noun, `s_` prefix for file-scope vars, global vars forbidden).
- ISRs: ≤50µs, no printf/blocking/float/allocation, only capture data + post to a queue/flag.
- No magic numbers — every threshold lives in `dg_config.h` (generated from `thresholds.yaml`).
- State machines = explicit `switch` over an enum with a `default:` that logs and enters the safe state.
- MISRA-C:2012 used as a selectively-enforced guideline (no claim of full compliance).
- **No floating point in the VCS control loop** — integer/fixed-point (Q16.16) only.

### Platform-specific lessons on MCXN947 (concrete, worth keeping handy while debugging)
- SDK drivers are opt-in via `prj.conf` — a missing `CONFIG_MCUX_COMPONENT_driver.<x>=y`
  produces an **undefined reference at link time**, not a compile error.
- Clock gating is manual (`CLOCK_EnableClock`) — a peripheral with no clock doesn't error, it
  silently reads back zero.
- `debug_console_lite`'s `PRINTF` **doesn't parse `%lu`** — prints the literal characters "lu".
  Use `%u` with an `(unsigned int)` cast.
- Out-of-tree apps: `PROJECT_BOARD_PORT_PATH` must be a **relative path** — an absolute path
  silently resolves wrong and `pin_mux.c` isn't found.

## Coding standards — Application (Python 3.13, not 3.11)
- `black` (line length 100) + `ruff`; `mypy --strict` on `domains/` and `fusion/`.
- **`fusion/` and `domains/` must be pure**: timestamped observations in, state out, no I/O, no
  internal clock reads → lets a 30-minute corpus recording replay through the exact production
  decision logic in under a second, deterministically, in CI.
- No hard-coded thresholds — always loaded from `thresholds.yaml`.
- The inference backend sits behind an `InferenceBackend` interface, with a `ReplayBackend` that
  reads pre-computed detections from a corpus file → everything above the backend is testable
  with no camera and no accelerator.
- `SIGTERM` must trigger a clean shutdown: publish `level=L0`, `calib_done=0`, disarm the VCS.

## Threshold single-source-of-truth rule
- `thresholds.yaml` is the single source of truth for every number in spec 03. CI runs
  `tools/check_thresholds.py` to compare the normative table in spec 03 against this file —
  a mismatch fails the build.
- Every threshold needs a `rationale:` field — a number with no reason can't be defended to a judge.

## Logging
- One line per event, key=value, machine-parseable, ISO8601 timestamp.
- Levels: ERROR/WARN/INFO/DEBUG (DEBUG off in release).
- **No images, no landmark coordinates in logs** — even in debug builds.
- MCU: never log from an ISR, must be rate-limited.
- Every benchmark run is logged to `docs/benchmarks/<run-id>/` with git SHA, threshold SHA-256, environment record.

## Build & Flash
- Each node has a `build.sh` that builds clean, no IDE required.
- Firmware prints version + git SHA (+`dirty` suffix if unclean) + build timestamp at boot.
- A `+dirty` build **must never** be used to produce a number recorded in spec 08.
- CI order: ICD regen diff check → threshold consistency check → lint/type-check → unit tests →
  corpus replay regression → build both firmwares → cppcheck/clang-tidy.
- VCS is flashed via `pyocd` with a pinned `PROBE_UID` (avoids an interactive prompt when
  multiple probes are attached).
- A udev rule allows flashing without sudo; if sudo is ever needed, it must keep `env HOME=$HOME`.
- Every deployment must read back the boot banner to confirm the correct SHA was deployed.

## Definition of Done (a checklist, no "mostly done")
Merged via reviewed PR + green CI · requirement IDs cited in the commit · unit tests added +
corpus replay regression still passes · new thresholds have a rationale · interface changes
regenerated on both sides · **verified on real hardware** · spec updated in the same PR if a
number changed · measurements recorded in spec 08 with an artefact directory · no new
⚠️ ASSUMPTION without being logged in Open Items.

## Change control
- Changing a normative threshold requires: old value, new value, evidence (benchmark run
  ID/corpus result), effect on the false-alarm rate, and 2 approvals.
- **DEV-092**: when measured reality contradicts a spec → update the spec in the same pull
  request that discovered it, never leave a known-false number in a document.

## Explicitly banned anti-patterns
Tuning a threshold until the live demo works · `sleep()` in the VCS control loop · catching a
broad `except Exception:` and continuing (turns a detector crash into "the driver is fine") ·
commenting out a failing test to unblock a merge · committing a measured number without its
artefact directory · demoing with a `+dirty` build · adding a second copy of a threshold "just for now."
