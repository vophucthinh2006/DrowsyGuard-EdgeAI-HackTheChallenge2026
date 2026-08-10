# dms-ap — Driver Monitoring System, an Arduino App Lab app

The DMS-AP half of **DrowsyGuard** (Qualcomm Future Makers 2026, team ML_IoT_Love50). Implements
[specs/02](../specs/02-development-standards.md)'s `dms-ap/` layout and
[specs/03](../specs/03-drowsiness-domain-spec.md) (the three detection domains + fusion ladder) on
top of the vision pipeline originally prototyped in `Custom_Blaze_Face+CNN/demo.py` (superseded,
see bottom of this file).

**Status: bring-up build.** `domains/`, `fusion/`, `link/icd.py`, `link/crc8.py` and `config.py`
are real, tested, and pass **61/61** unit tests with no camera, no App Lab, and no device required
(`pytest`, see below). The camera/model inference glue and the App Lab / RouterBridge integration
are real code but **have never run on a device** — no camera, no UNO Q, no App Lab available in
this environment. See "What is NOT verified" below before treating anything past the domain/fusion
layer as proven.

## App Lab

This app is structured to load directly into **Arduino App Lab**, the UNO Q's dual-core (MPU +
MCU) development environment — `app/app.yaml` + `app/python/` + `app/sketch/`. This structure and
every file in it are matched against a **real, published App Lab app**
([ShawnHymel/arduino_uno_q_blink_cli](https://github.com/ShawnHymel/arduino_uno_q_blink_cli) on
GitHub — its `app.yaml`, `python/main.py`, `sketch/sketch.ino` and `sketch/sketch.yaml` were
fetched and read, not guessed from a description) plus the platform docs at
[docs.arduino.cc](https://docs.arduino.cc/tutorials/uno-q/user-manual/) and the
[arduino/arduino-router](https://github.com/arduino/arduino-router) repo for the Bridge/RPC layer.

```
dms-ap/
├── app/                          <- the deployable App Lab app; App Lab syncs this to the device
│   ├── app.yaml                    App Lab manifest
│   ├── python/                     runs on the MPU (QRB2210, Debian Linux)
│   │   ├── main.py                   App Lab entry point
│   │   ├── requirements.txt          installed with uv when the app is deployed
│   │   ├── config/thresholds.yaml    moved here so it deploys WITH the app (see note below)
│   │   ├── models/                   .tflite files, same reason
│   │   └── drowsyguard/              the actual package (domains/fusion/inference/link/...)
│   └── sketch/                     runs on the MCU (STM32U585) -- this is DMS-RT, see below
│       ├── sketch.ino
│       └── sketch.yaml               Arduino CLI profile (fqbn: arduino:zephyr:unoq)
├── prototypes/face_mesh_ear_mar/  reference material, NOT deployed, NOT imported by app/
├── tests/                         61 tests, PYTHONPATH=app/python
└── pyproject.toml                 local dev/test packaging ONLY -- not what ships to the device
```

**Why `config/` and `models/` moved inside `app/python/`:** App Lab deploys the contents of `app/`
to the device as one unit. Anything the running app needs at runtime has to live inside `app/` —
a sibling `dms-ap/config/` would simply not be there on the device.

**Why there's no separate `dms-rt/` project:** the original spec 02 draft assumed DMS-RT (the
STM32U585 firmware) would be its own top-level project, mirroring `vcs-mcxn947/`. That didn't
survive contact with the real platform — App Lab requires the Python and the sketch halves in one
app folder, tied by one `app.yaml`. `app/sketch/` **is** DMS-RT. See
[specs/02 §2's revision note](../specs/02-development-standards.md#2-repository-layout) for the
full explanation of that correction.

### AP↔RT link: Arduino_RouterBridge (real, not a guess)

The single biggest open item from the previous revision of this project — "how does the Python
side talk to the STM32U585 side" — is a documented, first-class part of the UNO Q platform:
**Arduino_RouterBridge**, MessagePack-RPC over the internal serial line, brokered by App Lab's
router service. Confirmed API, from the real example fetched above:

```python
# python side
from arduino.app_utils import *
Bridge.provide("some_name", python_function)   # sketch can call this
Bridge.call("other_name", data)                 # calls a sketch-registered function
App.run(user_loop=loop)
```
```cpp
// sketch side
#include "Arduino_RouterBridge.h"
Bridge.begin();
Bridge.provide("other_name", cpp_function);
Bridge.call("some_name", data);
```

`link/ap_rt_transport.py`'s `RouterBridgeTransport` implements `ApRtTransport` on top of this —
encoding/decoding through the existing `link/icd.py` codec, sending `dms_status`/`dms_metrics`/
`emergency_stop` and receiving `vcs_status`/`vcs_event`. **What's still an assumption**, clearly
marked in that file's docstring: the exact C++ parameter type `Bridge.provide()` binds an incoming
MessagePack array to (`std::vector<uint8_t>` is an informed guess based on the sketch's confirmed
MsgPack/ArxContainer library dependencies, not a verified fact) — this is a one-function fix in
`_send()`/the sketch's handler signatures if wrong, not a redesign.

**What `app/sketch/sketch.ino` does NOT do yet:** put anything on the physical FDCAN1 bus. That
peripheral is what DMS-RT is supposed to own per
[specs/04 §1.1](../specs/04-interface-control-document.md#11-node-hardware-mapping) (OI-04-01,
still open), and there is an **open, unresolved Arduino forum thread** on exactly "getting FDCAN
working on the UNO Q" as of the research that went into this file — a real, currently-open problem
on Arduino's own platform, not something this project failed to research. The sketch proves the
Bridge link itself works (LED + Serial print on receipt) and leaves FDCAN1 as an explicit next
step, with a warning against copying a bare-metal STM32 CAN library: this board's sketch.yaml FQBN
is `arduino:zephyr:unoq` — a **Zephyr-based** Arduino core, not bare-metal STM32duino, so the usual
STM32 CAN libraries may not even apply.

### tflite-runtime, not tensorflow

`app/python/requirements.txt` and `pyproject.toml` use `tflite-runtime` (falls back to full
`tensorflow` only on x86_64 dev machines). The QRB2210 is an embedded ARM SBC doing inference, not
training — `tflite-runtime` is the purpose-built, order-of-magnitude-smaller package for that job,
and the full `tensorflow` wheel may not even be prebuilt for this SoC/OS/Python combination. Not
verified installable on a real UNO Q (no device available); `inference/blazeface_cnn_backend.py`
tries `tflite_runtime` first and falls back to `tensorflow.lite`, so either works without a code
change.

## Quick start

```bash
# Full environment (camera + models + everything) -- bench/dev mode, not App Lab
uv venv --python 3.11 .venv && source .venv/bin/activate
uv pip install -e ".[dev]"
python app/python/main.py --preview          # live camera demo, needs a webcam

# Just the pure logic (domains/fusion/link) -- no camera, no models, fast
uv venv --python 3.11 .venv && source .venv/bin/activate
uv pip install pytest pyyaml
PYTHONPATH=app/python python -m pytest tests/ -v    # 61 tests, ~0.2s
```

`app/python/main.py` auto-detects its environment: if `arduino.app_utils` imports (i.e. it's
actually running under App Lab), it uses the App Lab entry pattern + `RouterBridgeTransport`.
Everywhere else (this dev environment, any bench machine), it falls back to the original
argparse + manual-loop bench mode with `NullTransport`.

## Prototypes — read this before touching D1 or D2

`prototypes/face_mesh_ear_mar/` is a **second, independent prototype** (not yet integrated as an
`InferenceBackend`) that showed up as a loose top-level folder and was filed here rather than left
scattered. It is **more capable than the integrated backend** in exactly the two places
`inference/blazeface_cnn_backend.py` is weakest:

| | `inference/blazeface_cnn_backend.py` (integrated) | `prototypes/face_mesh_ear_mar/` (not integrated) |
|---|---|---|
| Face model | MediaPipe BlazeFace (6 keypoints) + 2 trained CNNs | MediaPipe **Face Mesh** (478 landmarks) |
| Eye state | CNN classifier on an eye crop | **EAR** (Eye Aspect Ratio) — landmark geometry, `EAR < 0.22` for 15 frames |
| Mouth/yawn | CNN classifier on a mouth crop | **MAR** (Mouth Aspect Ratio) — landmark geometry, `MAR > 0.55` for 15 frames |
| Head pose (D1) | **not implemented** (`yaw_deg`/`pitch_deg` always `None`) | **implemented** — `facial_transformation_matrixes` + `cv2.RQDecomp3x3`, `yaw>25°`/`pitch>20°` for 20 frames |

That MAR/EAR/head-pose approach is much closer to what
[specs/03](../specs/03-drowsiness-domain-spec.md) originally assumed (§4.1 mentions MAR by name;
`config/thresholds.yaml`'s `d2_yawn.mar_open` was written for exactly this and is currently unused
by the integrated backend — see the note there). Its own thresholds (`EAR_THRESHOLD=0.22`,
`MAR_THRESHOLD=0.55`, `YAW_THRESHOLD=25`, `PITCH_THRESHOLD=20`) are close to but not identical to
`thresholds.yaml`'s values and have not been reconciled with them.

**This was not integrated in this pass** — it needs a new `inference/face_mesh_backend.py`
implementing `InferenceBackend` (same shape as `blazeface_cnn_backend.py`: read a frame, run the
model, fill in a `FrameObservation`, most importantly `yaw_deg`/`pitch_deg` this time), and that
is real integration work with real failure modes, not something to do without a camera to test it.
Doing so is the highest-leverage next step for this half of the project: it directly closes the D1
(distraction) gap and the D2 MAR gap in one piece of work. See
`prototypes/face_mesh_ear_mar/README.md` (original prototype documentation, in Vietnamese) for the
algorithm details.

## What's real and tested

Run `PYTHONPATH=app/python python -m pytest tests/ -v` (needs only `pytest` + `pyyaml`, nothing else):

- **`domains/`** — every rule in [specs/03 §3-§5](../specs/03-drowsiness-domain-spec.md) with a
  DOM-* ID has a corresponding test, mirroring [specs/07](../specs/07-test-cases.md) TC-DOM-*:
  glance noise floor, cumulative-vs-continuous distraction, indicator suppression, yawn event
  gating (single yawn never alarms), microsleep dwell thresholds, wall-clock-not-frame-count dwell,
  PERCLOS hysteresis, D3 UNAVAILABLE never reporting IDLE, CRITICAL's clear-dwell requirement.
- **`fusion/`** — the L0-L3 ladder, ack refractory/saturation, the L2→L3 no-ack escalation timer,
  L3 never auto-de-escalating, sensor-loss never mapping to L0. Mirrors TC-FUS-*.
- **`link/icd.py` + `crc8.py`** — byte-exact round-trips against hand-computed payloads, CRC
  rejection of bad frames, magic-byte rejection on EMERGENCY_STOP (CAN-051). **Same CRC-8 test
  vectors as `vcs-mcxn947/src/icd/crc8.c`**, both copied from `../shared/icd/crc_vectors.csv` — see
  `app/python/drowsyguard/link/README.md` and `../shared/icd/README.md` for why the two
  implementations are still hand-synced rather than generated (specs 02 DEV-002).
- **`tools/replay_corpus.py`** (repo-root `tools/`) works end to end: feeding a synthetic closure
  event through it correctly produces `L0 → L1 (at 800ms) → L2 (at 1500ms)`, i.e. the whole
  domains→fusion chain agrees with the spec on a case it wasn't unit-tested against directly.

## What is NOT verified (do not treat as done)

- **Nothing in this app has run on an actual UNO Q, in App Lab or otherwise.** No device was
  available in this environment. The App Lab structure and the RouterBridge API are matched
  against real, fetched examples (see "App Lab" above), which is a much stronger basis than a
  guess, but "matches the documentation" and "verified on hardware" are different claims — don't
  conflate them.
- **The live camera pipeline has never run.** `inference/blazeface_cnn_backend.py` is a refactor
  of the working `Custom_Blaze_Face+CNN/demo.py` prototype (same models, same crop geometry, same
  classification calls) but this environment has no camera and no `cv2`/`mediapipe`/`tflite-runtime`
  installed, so the refactor itself is unexercised.
- **Head pose (D1's yaw/pitch) is not implemented in the integrated backend.**
  `blazeface_cnn_backend.py` always returns `yaw_deg=None, pitch_deg=None`. A working
  implementation already exists at `prototypes/face_mesh_ear_mar/` — see "Prototypes" above — it
  just isn't wired into an `InferenceBackend` yet.
- **`sunglasses_detected` is always `False`** — no classifier exists for it.
- **FDCAN1 (the sketch's actual job) is not implemented** — see "AP↔RT link" above. The Bridge
  link itself is implemented but unverified on hardware.
- **MAR-based yawn detection (`d2_yawn.mar_open` in thresholds.yaml) is unused by the integrated
  backend.** A real MAR implementation exists at `prototypes/face_mesh_ear_mar/` with its own
  threshold (0.55, not yet reconciled with `thresholds.yaml`'s 0.60).
- **Confidence values are a decision-boundary-distance proxy**, not a calibrated estimate — see
  the "Known gaps" note in `blazeface_cnn_backend.py`. Feeds directly into D3's `EYE_CONF_MIN` gate.
- **`Bridge.provide()`'s handler parameter type is unverified** (see "AP↔RT link" above).
- **No corpus exists.** `inference/replay_backend.py` and `tools/replay_corpus.py` work (see
  above) but there is no recorded, annotated C-BASE/C-DROWSY/C-ADVERSE corpus per
  [specs/06 §5](../specs/06-test-plan.md#5-test-corpora) to run them against yet — only the
  synthetic smoke-test used to verify `tools/replay_corpus.py` works at all.
- `tools/check_thresholds.py` (specs 02 DEV-051 — CI check that `thresholds.yaml` matches specs
  03 §8 automatically) does not exist; `tests/test_config.py` spot-checks a subset by hand instead.

## `Custom_Blaze_Face+CNN/` is superseded

That folder's `demo.py` logic now lives in `app/python/drowsyguard/inference/blazeface_cnn_backend.py`
+ `debug_overlay.py` + `capture/camera.py`, and its `models/` moved to `app/python/models/`. The
original folder has been removed from the repo rather than kept as a stale duplicate.
