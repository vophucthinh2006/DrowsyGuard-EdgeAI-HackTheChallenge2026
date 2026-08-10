# dms-ap — Driver Monitoring System, application processor (Arduino UNO Q / QRB2210)

The DMS-AP half of **DrowsyGuard** (Qualcomm Future Makers 2026, team ML_IoT_Love50). Implements
[specs/02](../specs/02-development-standards.md)'s `dms-ap/` layout and
[specs/03](../specs/03-drowsiness-domain-spec.md) (the three detection domains + fusion ladder) on
top of the vision pipeline originally prototyped in `Custom_Blaze_Face+CNN/demo.py` (now folded
into `src/drowsyguard/inference/`, see below — that folder is superseded by this one).

**Status: bring-up build.** `domains/`, `fusion/`, `link/icd.py`, `link/crc8.py` and `config.py`
are real, tested, and pass **61/61** unit tests with no camera or model files required
(`pytest`, see below). The camera/model inference glue (`inference/blazeface_cnn_backend.py`) is
refactored from a working prototype but **cannot be exercised in this environment** — no camera,
no `cv2`/`mediapipe`/`tensorflow` available here — so it is untested since the refactor. See
"What is NOT verified" below before treating anything past the domain/fusion layer as proven.

## Quick start

```bash
# Full environment (camera + models + everything)
uv venv --python 3.11 .venv && source .venv/bin/activate
uv pip install -e ".[dev]"
python -m drowsyguard.app --preview          # live camera demo, needs a webcam

# Just the pure logic (domains/fusion/link) -- no camera, no models, fast
uv venv --python 3.11 .venv && source .venv/bin/activate
uv pip install pytest pyyaml
PYTHONPATH=src python -m pytest tests/ -v    # 61 tests, ~0.2s
```

## Layout

```
dms-ap/
├── config/thresholds.yaml     # THE single source of every domain/fusion number (DEV-050)
├── models/                    # moved from Custom_Blaze_Face+CNN/models/
├── prototypes/
│   └── face_mesh_ear_mar/     # a SECOND, un-integrated prototype -- read "Prototypes" below,
│                                 it directly closes two of the gaps listed further down
├── src/drowsyguard/
│   ├── config.py                # loads thresholds.yaml -> typed dataclasses
│   ├── capture/camera.py         # camera abstraction (day/night switch point)
│   ├── inference/
│   │   ├── backend.py             # InferenceBackend interface (DEV-044)
│   │   ├── blazeface_cnn_backend.py  # the integrated backend -- BlazeFace + 2 tiny CNNs
│   │   ├── replay_backend.py      # feeds a recorded JSONL corpus through the SAME pipeline
│   │   └── debug_overlay.py       # optional on-screen view (--preview), bench only
│   ├── domains/                  # D1/D2/D3 -- pure, no I/O, no camera, no clock read (DEV-042)
│   │   ├── d1_distraction.py
│   │   ├── d2_yawn.py
│   │   └── d3_eyeclosure.py
│   ├── fusion/ladder.py          # L0-L3 alert ladder
│   ├── link/
│   │   ├── icd.py                 # wire format, implements ../../../shared/icd/icd.yaml
│   │   ├── crc8.py                # same CRC-8, vectors copied from ../../../shared/icd/crc_vectors.csv
│   │   ├── ap_rt_transport.py      # interface stub -- see link/README.md, this is the honest gap
│   │   └── README.md               # explains exactly what's missing and why
│   ├── telemetry/logger.py       # structured key=value logging (DEV-060/062)
│   └── app.py                    # main loop: capture -> inference -> domains -> fusion -> link
└── tests/                        # 61 tests, all passing, zero external dependencies
```

`tools/replay_corpus.py` (CLI: replay a JSONL corpus through the real pipeline) lives at the
**repo-root** `tools/`, not here — see [specs/02 §2](../specs/02-development-standards.md#2-repository-layout).

## Prototypes — read this before touching D1 or D2

`prototypes/face_mesh_ear_mar/` is a **second, independent prototype** (not yet integrated as an
`InferenceBackend`) that showed up as a loose top-level folder and was filed here rather than left
scattered. It is **more capable than the integrated backend** in exactly the two places
`blazeface_cnn_backend.py` is weakest:

| | `inference/blazeface_cnn_backend.py` (integrated) | `prototypes/face_mesh_ear_mar/` (not integrated) |
|---|---|---|
| Face model | MediaPipe BlazeFace (6 keypoints) + 2 trained CNNs | MediaPipe **Face Mesh** (478 landmarks) |
| Eye state | CNN classifier on an eye crop | **EAR** (Eye Aspect Ratio) — landmark geometry, `EAR < 0.22` for 15 frames |
| Mouth/yawn | CNN classifier on a mouth crop | **MAR** (Mouth Aspect Ratio) — landmark geometry, `MAR > 0.55` for 15 frames |
| Head pose (D1) | **not implemented** (`yaw_deg`/`pitch_deg` always `None`) | **implemented** — `facial_transformation_matrixes` + `cv2.RQDecomp3x3`, `yaw>25°`/`pitch>20°` for 20 frames |

That MAR/EAR/head-pose approach is much closer to what
[specs/03](../specs/03-drowsiness-domain-spec.md) originally assumed (§4.1 mentions MAR by name;
`thresholds.yaml`'s `d2_yawn.mar_open` was written for exactly this and is currently unused by the
integrated backend — see the note there). Its own thresholds (`EAR_THRESHOLD=0.22`,
`MAR_THRESHOLD=0.55`, `YAW_THRESHOLD=25`, `PITCH_THRESHOLD=20`) are close to but not identical to
`config/thresholds.yaml`'s values and have not been reconciled with them.

**This was not integrated in this pass** — it needs a new `inference/face_mesh_backend.py`
implementing `InferenceBackend` (same shape as `blazeface_cnn_backend.py`: read a frame, run the
model, fill in a `FrameObservation`, most importantly `yaw_deg`/`pitch_deg` this time), and that
is real integration work with real failure modes, not something to do inside a reorganization pass
with no camera available to test it. Doing so is the highest-leverage next step for this half of
the project: it directly closes the D1 (distraction) gap and the D2 MAR gap in one piece of work.
See `prototypes/face_mesh_ear_mar/README.md` (original prototype documentation, in Vietnamese) for
the algorithm details.

## What's real and tested

Run `PYTHONPATH=src python -m pytest tests/ -v` (needs only `pytest` + `pyyaml`, nothing else):

- **`domains/`** — every rule in [specs/03 §3-§5](../specs/03-drowsiness-domain-spec.md) with a
  DOM-* ID has a corresponding test, mirroring [specs/07](../specs/07-test-cases.md) TC-DOM-*:
  glance noise floor, cumulative-vs-continuous distraction, indicator suppression, yawn event
  gating (single yawn never alarms), microsleep dwell thresholds, wall-clock-not-frame-count dwell,
  PERCLOS hysteresis, D3 UNAVAILABLE never reporting IDLE, CRITICAL's clear-dwell requirement.
- **`fusion/`** — the L0-L3 ladder, ack refractory/saturation, the L2→L3 no-ack escalation timer,
  L3 never auto-de-escalating, sensor-loss never mapping to L0. Mirrors TC-FUS-*.
- **`link/icd.py` + `crc8.py`** — byte-exact round-trips against hand-computed payloads, CRC
  rejection of bad frames, magic-byte rejection on EMERGENCY_STOP (CAN-051). **Same CRC-8 test
  vectors as `vcs-mcxn947/src/icd/crc8.c`**, both copied from `shared/icd/crc_vectors.csv` — see
  `link/README.md` and `../shared/icd/README.md` for why the two implementations are still
  hand-synced rather than generated (specs 02 DEV-002).
- **`tools/replay_corpus.py`** (repo-root `tools/`) works end to end: feeding a synthetic closure
  event through it correctly produces `L0 → L1 (at 800ms) → L2 (at 1500ms)`, i.e. the whole
  domains→fusion chain agrees with the spec on a case it wasn't unit-tested against directly.

## What is NOT verified (do not treat as done)

- **The live camera pipeline has never run.** `inference/blazeface_cnn_backend.py` is a refactor
  of the working `Custom_Blaze_Face+CNN/demo.py` prototype (same models, same crop geometry, same
  classification calls) but this environment has no camera and no `cv2`/`mediapipe`/`tensorflow`
  installed, so the refactor itself is unexercised. Test it with `python -m drowsyguard.app
  --preview` on real hardware before trusting it.
- **Head pose (D1's yaw/pitch) is not implemented in the integrated backend.**
  `blazeface_cnn_backend.py` always returns `yaw_deg=None, pitch_deg=None` — BlazeFace's 6
  keypoints alone don't give a validated pose estimate without solvePnP + a 3D face model. **A
  working implementation of this already exists** at `prototypes/face_mesh_ear_mar/` — see
  "Prototypes" above — it just isn't wired into an `InferenceBackend` yet. D1's domain logic
  itself is complete and tested; it just never receives real evidence today.
- **`sunglasses_detected` is always `False`** — no classifier exists for it.
- **The AP↔RT transport does not exist.** `link/ap_rt_transport.py` only has `NullTransport`
  (does nothing). The CAN controller is on the STM32U585 (DMS-RT), not reachable from this Linux
  code directly, and the mechanism the Arduino UNO Q uses to bridge its two cores hasn't been
  established in this project yet. **Read `src/drowsyguard/link/README.md` — this is the single
  most important open item in this half of the system**, more so than any individual missing
  feature above.
- **MAR-based yawn detection (`d2_yawn.mar_open` in thresholds.yaml) is unused by the integrated
  backend.** It classifies mouth state with a CNN, not landmark geometry. A real MAR implementation
  exists at `prototypes/face_mesh_ear_mar/` (see "Prototypes" above) with its own threshold (0.55,
  not yet reconciled with `thresholds.yaml`'s 0.60).
- **Confidence values are a decision-boundary-distance proxy**, not a calibrated estimate — see
  the "Known gaps" note in `blazeface_cnn_backend.py`. This feeds directly into D3's
  `EYE_CONF_MIN` gate and is worth revisiting once real corpus data exists.
- **No corpus exists.** `inference/replay_backend.py` and `tools/replay_corpus.py` work (see
  above) but there is no recorded, annotated C-BASE/C-DROWSY/C-ADVERSE corpus per
  [specs/06 §5](../specs/06-test-plan.md#5-test-corpora) to run them against yet — only the
  synthetic smoke-test used to verify `tools/replay_corpus.py` works at all.
- `tools/check_thresholds.py` (specs 02 DEV-051 — CI check that `thresholds.yaml` matches specs
  03 §8 automatically) does not exist; `tests/test_config.py` spot-checks a subset by hand instead.
- No `dms-rt/` STM32U585 firmware project exists yet — see `link/README.md`, step 3.

## `Custom_Blaze_Face+CNN/` is superseded

That folder's `demo.py` logic now lives in `inference/blazeface_cnn_backend.py` +
`inference/debug_overlay.py` + `capture/camera.py`, and its `models/` moved to `dms-ap/models/`.
The original folder has been removed from the repo rather than kept as a stale duplicate.
