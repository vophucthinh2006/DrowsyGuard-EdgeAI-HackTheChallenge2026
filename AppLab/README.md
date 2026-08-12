# Driver Monitoring - Arduino App Lab (UNO Q, Qualcomm)

This is the same face-landmark / drowsiness-monitoring pipeline from the
earlier PyCharm project, reorganized to match Arduino App Lab's project
layout (`python/`, `sketch/`, `app.yaml`) and fully translated to English
(comments + on-screen text). Core logic is unchanged from the version you
tested on PC.

## Do the .tflite models need to be converted to .eim?

**No - and in this case you should NOT convert them.** Short version:

- `.eim` is **Edge Impulse's own compiled model format** - a full
  executable (not just weights) that Edge Impulse Studio compiles for one
  specific CPU architecture (e.g. Linux ARM64) from a model **trained
  inside Edge Impulse Studio**. It's what the official "Object Detection"
  Brick (`EdgeImpulseRunnerFacade`, shown in your screenshot) talks to over
  a local socket.
- To get an `.eim` file you would need to **re-create these models inside
  Edge Impulse Studio** (upload data, train there, or "Bring Your Own
  Model" import), matching one of Edge Impulse's supported task types
  (classification, FOMO object detection, visual anomaly detection).
- Your pipeline doesn't fit that mold: `face_detector.tflite` is a raw SSD
  detector needing custom anchor-decoding, `face_landmarks_detector.tflite`
  is a 478-point regression head, and the drowsiness/head-pose logic is
  hand-written on top. Forcing all of that through Edge Impulse Studio
  would mean rebuilding/retraining everything there - a large, lossy
  detour just to satisfy one Brick's input format.
- The good news: you don't have to. Since **App Lab 0.7**, you can write a
  **Custom Brick** (or, as done here, a plain Python app) that runs on the
  board's Linux (MPU) side - which is just Debian with `pip` - so the exact
  same `ai_edge_litert` / `tflite_runtime` code that ran on your PC runs
  unchanged on the UNO Q. `.eim` is only a requirement if you specifically
  want to use Arduino's pre-built "Object Detection" Brick with an
  Edge-Impulse-trained model - not a general requirement for running
  `.tflite` files on the board.

## Project layout

```
AppLabApp/
├── app.yaml              # App Lab manifest (see note inside - verify against current App Lab)
├── sketch/
│   └── sketch.ino         # Empty placeholder - this app doesn't use the MCU/Zephyr side
└── python/                # Everything here runs on the Linux (MPU) side
    ├── main.py             # Entry point - camera loop, drawing, alerts
    ├── blaze_face.py        # face_detector.tflite wrapper (BlazeFace + anchor decode + NMS)
    ├── anchors.py            # SSD anchor generation for BlazeFace
    ├── face_landmark.py      # face_landmarks_detector.tflite wrapper (478 3D landmarks)
    ├── landmark_regions.py   # Landmark index groups (eyes/mouth/head-pose points)
    ├── drowsiness.py          # EAR/MAR/head-pose (solvePnP) drowsiness logic
    ├── obstruction_detector.py # custom_mobilenetv3_v1_float16.tflite wrapper (mask/sunglasses)
    ├── tflite_backend.py      # Picks whichever TFLite runtime is installed
    ├── webui.py                # Built-in MJPEG web viewer (no extra dependency)
    ├── requirements.txt
    └── models/                # All .tflite files unchanged, plus the geometry .binarypb (unused)
```

## How to import this into App Lab

1. In Arduino App Lab, create a new blank app (or use "Copy and edit app"
   on any example) so App Lab generates a correct, current `app.yaml` and
   folder skeleton for your installed App Lab version.
2. Replace the generated `python/` folder's contents with everything in
   this project's `python/` folder (including `models/`).
3. Compare the generated `app.yaml` against the one included here and merge
   any fields App Lab expects that aren't listed above (the schema may have
   evolved since this was written).
4. Leave `sketch/sketch.ino` as-is (or use whatever empty sketch App Lab
   generated) - no MCU code is needed for this app.
5. Connect a USB webcam to the UNO Q's USB hub, press Run.

## Viewing the output (native window vs. web view)

Arduino App Lab Python apps normally run inside a **headless Docker
container** - there's no X server for `cv2.imshow()` to draw into. Rather
than assume `cv2.imshow` will work, `main.py` **auto-detects** this:

- Running locally on your PC (with a real desktop) -> a normal OpenCV
  window opens, press `q` to quit, exactly like before.
- Running inside App Lab's container on the board -> `main.py` starts a
  tiny built-in web server instead (`webui.py`, no extra dependency).
  Open a browser to `http://<board-ip>:7000/` to see the live annotated
  video - this mirrors how Arduino's own example apps expose video
  (their "WebUI - HTML" Brick uses the same port 7000 convention).

## Installing dependencies - what actually happens

App Lab **does** auto-install from `requirements.txt` when you press Run
(first run is slower - it needs internet on the board to download the
Docker base image and the packages). However, there is a real, confirmed
risk worth checking before you rely on this:

- App Lab currently defaults to **Python 3.13**. Some compiled ML packages
  (this has been specifically reported for `mediapipe`, and may also apply
  to `ai-edge-litert` / `tflite-runtime`) **don't yet ship prebuilt wheels
  for Python 3.13 on aarch64**. If that happens, `pip install` inside
  `requirements.txt` will simply fail on first run.
- If that happens, options (roughly in order of effort):
  1. Try the next backend in `requirements.txt` (`tflite-runtime`, then
     `tensorflow`) - one of the three usually has an aarch64 wheel even if
     the others don't.
  2. Pin an older Python version for the app using App Lab's `uv`-based
     environment override (documented on the Arduino forum thread "Using a
     Custom Python Version in App Lab").
  3. SSH into the board (App Lab exposes a full Debian shell) and run
     `pip3 install <package>` manually first, to see the exact error before
     troubleshooting inside App Lab.
- Recommended first step on real hardware: SSH in and run
  `pip3 install -r python/requirements.txt` by hand once, so you see any
  build errors directly in a normal terminal instead of buried in App Lab's
  console output.

## Performance tuning (same knobs as before)

At the top of `main.py`:

```python
NUM_THREADS = 4        # set to the board's actual CPU core count
DETECT_EVERY = 15      # 1 = detect every frame; higher = smoother but less precise tracking
CAM_WIDTH = 1280
CAM_HEIGHT = 720
```

## What did NOT change

- All model files in `models/` are byte-for-byte identical to the ones you
  uploaded - nothing was retrained, requantized, or converted.
- The inference logic (`blaze_face.py`, `face_landmark.py`,
  `obstruction_detector.py`) is the same code that ran on PC, just with
  English comments/strings.
