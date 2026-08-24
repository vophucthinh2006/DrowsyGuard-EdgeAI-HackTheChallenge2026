# DrowsyGuard — DMS (Driver Monitoring System)

DrowsyGuard is a real-time driver drowsiness detection system built on the **Arduino UNO Q**. It runs continuous eye-state / yawn inference on a USB camera feed, makes a time-based alert decision on the MCU, drives a physical buzzer, and broadcasts vehicle-safety status over **CAN bus** to a companion vehicle controller (`vcs-mcxn947`). A live dashboard in the browser mirrors the alert state, vehicle speed, and the raw AI detections.

*This app is a heavily modified fork of Arduino's `object-hunting` example — it now shares almost none of the original game logic. See [Related Projects](#related-projects) for the rest of the DMS stack.*

## System Architecture

```
 USB Camera
     │
     ▼
 Camera + video_object_detection Brick   (Edge Impulse YOLOX Nano, model ei-model-1086456-7)
     │  detections: { closed_eye, open_eye, yawning }  (per class: list of {confidence, bbox})
     ▼
 main.py                                          MPU / Linux side (QRB2210)
   ├─ per-class confidence filtering (CLASS_THRESHOLDS)
   ├─ smoothed Inference FPS (15-sample rolling average)
   ├─ bounding boxes + FPS + detection log  ───────────────►  WebSocket  ───►  Browser UI
   └─ latest detection bundle
        └─► bridge-tx thread (non-blocking, coalesced)
                └─► Bridge.call("send_dms_bundle", frame_id, "label:conf,...")
                                │
                                ▼
 sketch.ino                                       MCU side (STM32U5, Zephyr)
   ├─ millis()-based drowsiness state machine  →  alert level 0–3
   ├─ buzzer (tone()/noTone() on pin 8)
   ├─ DMS_STATUS (CAN ID 0x100), every 100 ms, fixed cadence  ─►  FDCAN1  ─►  vcs-mcxn947
   ├─ VCS_STATUS (CAN ID 0x200)  ◄───────────────────────────────────────────┘  (wheel duty % → simulated km/h)
   └─ telemetry (alert_level, speed, eye/yawn counters)
        └─► Bridge.notify("on_mcu_telemetry", ...) ─► main.py ─► WebSocket ─► Browser UI
```

The camera pipeline (MPU/Python) and the decision engine (MCU/sketch) run on two different processors bridged by Arduino's `Arduino_RouterBridge`. This split matters: inference is slow and non-deterministic (a few hundred ms/frame), while the CAN link to the vehicle controller expects a steady 100 ms heartbeat — so the alert decision, the buzzer, and the CAN heartbeat all live on the MCU and run on their own clock, independent of camera FPS.

## Bricks Used

- `web_ui` — serves the dashboard, handles WebSocket messaging with the browser.
- `video_object_detection` — manages the USB camera, runs the on-device inference model, hosts a live MJPEG preview on port `4912`.

## Hardware Requirements

- Arduino UNO Q (x1) or Arduino VENTUNO Q (x1)
- **USB-C® hub with external power (x1)** *(required on UNO Q to power the camera)*
- Power supply for the hub (5 V, 3 A)
- **USB webcam** (x1) — must be connected **before** the App is started, or it will fail to launch
- CAN transceiver wired to **D4 (PA12, TX)** / **D5 (PA11, RX)** for the FDCAN1 link to `vcs-mcxn947` (500 kbit/s classic CAN). Without this link, vehicle speed reads 0 and the buzzer/alerts still work locally.

## Alert Levels

The decision engine (`sketch.ino`) tracks how long the driver's eyes have been continuously closed / how long a yawn has lasted, using `millis()` — not frame counts, so the thresholds below hold regardless of camera FPS or dropped frames.

| Level | Code | Condition | Buzzer | UI |
|---|---|---|---|---|
| L0 — Normal | 100 | default | off | "MONITORING" |
| L1 — Advisory | 101 / 102 | yawning ≥ 1.5 s, **or** eyes closed 1.0–2.0 s | short 1000 Hz beep | Advisory banner |
| L2 — Warning | 200 | eyes closed 2.0–4.0 s | 2000 Hz beep | Warning ring |
| L3 — Danger | 300 | eyes closed ≥ 4.0 s | sustained 3000 Hz tone | Fullscreen danger modal |

`alert_level` (0–3) is also encoded into the `DMS_STATUS` CAN frame (ID `0x100`) sent every 100 ms to `vcs-mcxn947`, independent of the camera/inference rate.

Tapping anywhere on the alert overlay sends `dismiss_alert` back to the MCU, which resets the counters, silences the buzzer, and pushes a clean `L0` telemetry frame.

## Web Interface

Two tabs, served at `http://<UNO-Q-IP>:7000`:

- **Dashboard Cluster** — a car-instrument-cluster style view: live speedometer (driven by `VCS_STATUS` over CAN), alert overlay with three severity states, tap-to-dismiss.
- **Camera AI Vision** — live camera feed (embedded via `<iframe>` from the Brick's own MJPEG stream on port `4912`), bounding boxes drawn on a canvas overlay, Inference FPS, detection count, a live detection log, and a confidence threshold slider.

## Configuration

- **Per-class confidence thresholds** — `CLASS_THRESHOLDS` in `python/main.py` (`closed_eye`, `yawning`, `open_eye`, plus a `default`). Adjustable at runtime from the UI slider (`override_th`) or via the `override_class_th` WebSocket message for per-label tuning.
- **Camera capture rate** — `Camera(fps=8)` in `python/main.py`. This is a *request* to the USB driver, not a guarantee — the driver may round to its nearest supported mode.
- **Alert timing** — `TARGET_EYE_WARN_MS` / `TARGET_EYE_ALARM_MS` / `TARGET_EYE_DANGER_MS` / `TARGET_YAWN_WARN_MS` in `sketch/sketch.ino`.
- **Detection model** — set in `app.yaml` (`arduino:video_object_detection: model: ei-model-1086456-7`), an Edge Impulse-trained YOLOX Nano model ([Edge Impulse Studio Live Project](https://studio.edgeimpulse.com/public/1095447/live)) for `closed_eye` / `open_eye` / `yawning`.

## Getting Started

1. **Hardware setup** — connect the USB webcam to a powered USB-C hub on the UNO Q; wire the CAN transceiver to D4/D5 if you want live vehicle speed and CAN-side alerts.
2. **Run the App** from Arduino App Lab. If it exits immediately, the camera likely isn't detected — check the hub's power connection.
3. **Open the dashboard** at `http://<UNO-Q-IP-ADDRESS>:7000`.
4. Switch to **Camera AI Vision** to see the live feed and detection log; use the confidence slider to tune sensitivity.

## Troubleshooting

**App fails to start / exits immediately**
Almost always a missing USB camera. Confirm it's plugged into a *powered* USB-C hub before launching.

**Video feed is black or stuck on "Searching Camera Stream..."**
The feed is a separate MJPEG stream on port `4912`, embedded via `<iframe>`. Confirm your browser isn't blocking that port and that you're on the same network as the board.

**Detections seem inaccurate / too sensitive**
Lower or raise the Confidence Threshold slider. Per-label thresholds (`CLASS_THRESHOLDS`) can be tuned in `main.py` if one class (e.g. `closed_eye`) needs to be more sensitive than others.

**Vehicle speed always shows 0**
The MCU only reports real speed once it has received at least one `VCS_STATUS` CAN frame from `vcs-mcxn947`. Check the CAN wiring and that `vcs-mcxn947` is powered and running; the serial log will print `[CAN] FDCAN1 up 500 kbit/s` on successful init.

**Alerts feel delayed or fire too early after changing camera FPS**
Shouldn't happen — alert timing is measured in real time (`millis()`) on the MCU, not in frame counts, specifically so it stays correct regardless of camera FPS. If you *do* see drift, check for MCU-side clock issues rather than the Python pipeline.

## Known Limitations

- **Camera FPS shown in the UI is the configured target, not a live measurement.** Two attempts to measure the true capture rate (browser-side `fetch()` of the MJPEG stream, then a server-side `cv2.VideoCapture` proxy) both failed on real hardware — the stream endpoint doesn't send CORS headers, and the same URL turned out to serve an HTML wrapper page rather than raw MJPEG. The `<iframe>` preview itself is unaffected and works normally; only the numeric FPS readout is a config echo.
- **`Bridge.call()` thread-safety from a background thread is assumed, not formally documented.** The Python side offloads `Bridge.call()` to a dedicated thread so a slow/blocked MCU call can't stall the inference callback; this has run correctly in on-device testing but isn't confirmed against Arduino's own documentation.
- **Vehicle speed is simulated.** `vcs-mcxn947` has no physical speed sensor; speed is derived from motor duty-cycle percentage sent over CAN, not a real wheel encoder.
- **`app.yaml`'s `name`/`description` still reflect the original object-hunting template** and haven't been updated to match this app.

## Related Projects

- **`vcs-mcxn947`** — the vehicle controller this app talks to over CAN (`VCS_STATUS`/`DMS_STATUS`, IDs `0x200`/`0x100`).
- **`dms-ap-uno-q`** — a sibling DMS implementation using a different AP↔RT transport/ICD layer.

## License

SPDX-License-Identifier: MPL-2.0 — Copyright (C) Arduino S.r.l. and/or its affiliated companies (original template), with substantial modifications for the DrowsyGuard DMS application.
