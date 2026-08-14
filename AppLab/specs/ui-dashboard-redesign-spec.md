# AppLab On-Device Display — Car-Dashboard UI Redesign Spec

**Document:** APPLAB-UI-SPEC-01 · Rev 0.1 · 2026-08-14 · DRAFT — PENDING APPROVAL
**Applies to:** `AppLab/python/webui.py` + `AppLab/python/main.py` (the actual on-board web view
served by the UNO Q at `http://<board-ip>:7000/`)

This is a **design spec only**. No code is changed by this document. Once approved, implementation
happens as a separate step.

---

## 1. Why this doc exists / scope note

There are **two different "UI" things** in this repo — do not conflate them:

1. `specs/09-ui-dashboard-spec.md` — a **desktop Electron demo app** driven by CAN + a Rust bridge,
   meant for judges to watch the *vehicle* (speed, RPM, VCS alerts) on a laptop. Not on the board.
2. **This document** — the **on-device web page the UNO Q itself serves**, viewed by opening
   `http://<board-ip>:7000/` in a browser (phone/laptop on the same network, or a screen wired to
   the board). This is what the driver/demo booth actually sees live. Today this is a bare `<img>`
   tag on a black page (`AppLab/python/webui.py` `_INDEX_HTML`) showing the raw camera frame with
   OpenCV text burned into the pixels (`AppLab/python/main.py` lines ~157-215). **This is the part
   being redesigned.**

**Revision note (Rev 0.2, this pass):** the first draft of this spec put the live driver camera
front-and-center with detection overlays (face box, landmarks) and EAR/MAR/pose telemetry visible
on screen. **Rejected** — the page should look and behave like an actual **vehicle instrument
cluster** (speed, indicators, warning lamps), the same visual language as `specs/09` describes for
the desktop demo, not a driver-monitoring debug view. The camera feed and every detection-model
readout (EAR, MAR, pitch/yaw, face box, landmarks) are **removed from the UI entirely**. The
drowsiness pipeline still runs exactly as before in the background — it just never renders its own
output; only the derived `L1`/`L2`/`L3` alert level reaches the page, exactly like a real car's
driver-attention warning lamp never shows you a camera feed of your own face.

### 1.1 Current state (as-is, for reference)

- `webui.py`: dependency-free `http.server` MJPEG streamer. Serves `/` (plain HTML shell) and
  `/stream.mjpg` (multipart JPEG stream). No other endpoint exists.
- `main.py`: draws face mesh points, eye/mouth outlines, bbox, and **all status text directly onto
  the video frame** with `cv2.putText`/`cv2.rectangle` before it's JPEG-encoded and streamed —
  `Status: {ALERT|DANGER}`, warning strings, EAR/MAR/pitch/yaw numbers, FPS. All styling is fixed
  OpenCV font rendering, not CSS — cannot be restyled without touching the Python drawing code and
  re-encoding every frame.
- `drowsiness.py`: `DrowsinessMonitor.update()` only distinguishes **two** states today —
  `"ALERT"` (green) and `"DANGER"` (red) — driven by three independent trigger flags (EAR
  sustained / MAR sustained / pose sustained), no severity ladder.
- No JSON/telemetry endpoint exists — the browser has no way to know *why* a warning fired except
  by reading the burned-in text.

### 1.2 Relationship to the "real" fusion ladder (`dms-ap/`)

The sibling package `dms-ap/app/python/drowsyguard/fusion/ladder.py` already defines the correct,
spec'd 4-level model (`L0_NORMAL` / `L1_EARLY` / `L2_DROWSY` / `L3_DANGER`, per
`specs/03-drowsiness-domain-spec.md`) with a key rule this redesign deliberately reuses:

> **DOM-000** — only sustained eye-closure (D3) can escalate to L3. Yawning (D2) and
> head-pose/distraction (D1) are capped at L2, even at maximum severity, because they're
> *predictive* signals, not proof of imminent danger.

`AppLab/` is a separate, standalone prototype (not the `dms-ap` package) with its own simpler
EAR/MAR/pose logic — it has no domain state machine to reuse directly. This spec adds a **small,
local 3-level classifier** to `drowsiness.py` that mirrors the *shape* of DOM-000 without pulling
in the full `dms-ap` fusion machinery, so the two prototypes stay conceptually consistent.

---

## 2. Design goals

- **G1 — Look and behave like a real vehicle instrument cluster**, not a driver-monitoring debug
  view: a speedometer (and optionally a secondary gauge/odometer/indicator strip, see §4), dark
  automotive theme, drowsiness alerts surfaced only as warning lamps/banners layered on top —
  never a camera feed, never a raw EAR/MAR/pitch/yaw number anywhere on screen.
- **G2 — Exactly 3 alert severities, L1/L2/L3**, each with a visually and behaviourally distinct
  treatment (color, placement, motion/animation, sound), matching the L1/L2/L3 semantics already
  established in `specs/09-ui-dashboard-spec.md` §6 so the two UIs in this repo "read" the same way
  to anyone who has seen either one.
- **G3 — Stop baking readouts into video pixels.** No detection output should be rendered onto
  frames at all in this page's data path (see §5) — the only thing that crosses from the detection
  pipeline into the UI is the single `level` value.
- **G4 — Stay dependency-free.** `webui.py` today uses only `http.server` + stdlib (explicitly to
  avoid a Flask dependency on the board). This redesign keeps that constraint: one more stdlib
  `http.server` route for JSON, plain SVG/CSS/vanilla JS in the browser, no npm build step.
- **G5 — Reuse the project's established palette** (navy `#0B2A52`, gold/amber `#E8A33D`, coral/red
  `#EE6C4D`–`#D93A3A`) from `beamerthemeMLIOT.sty` / the A1 poster, so the on-device UI, the poster,
  and the pitch deck feel like one product family instead of three different visual languages.

### Non-goals

- No change to the detection models, camera pipeline, or `.tflite` inference.
- No change to `dms-ap`'s fusion ladder — this only affects the standalone `AppLab/` prototype.
- No audio hardware integration beyond what a browser can do (`<audio>`/WebAudio beep) — this is a
  demo-booth browser page, not the VCS buzzer.
- No CAN/VCS linkage — `AppLab/` has no transport to the VCS today (`sketch.ino` is a placeholder);
  out of scope for this spec. **Consequence: this prototype has no real vehicle speed sensor to
  read from** — see §4.1 for how the speedometer is driven.
- The MJPEG camera stream (`/stream.mjpg`) is **not deleted** from the code (still useful for
  bench debugging), it's just **no longer linked from the main dashboard page** — see §5.

---

## 3. Alert level model (L1 / L2 / L3)

### 3.1 Inputs available today (unchanged)

Per frame, `drowsiness.py` already computes: `avg_ear`, `mar`, `pitch`, `yaw`, plus three
consecutive-frame counters (`ear_counter`, `mar_counter`, `pose_counter`) against thresholds
`EAR_THRESHOLD=0.22`/15 frames, `MAR_THRESHOLD=0.55`/15 frames, `YAW/PITCH` 25°/20°/20 frames.

### 3.2 New classifier rule (proposed)

| Level | Name | Trigger condition | Escalation source |
|---|---|---|---|
| **L0** | Normal | No counter is above 0, or below "early" threshold below | — (not an "alert", dashboard shows calm/idle state) |
| **L1** | Early / Advisory | Any **one** counter is **building but not yet fully tripped** — i.e. `counter >= 0.4 * CONSECUTIVE_FRAMES` for EAR/MAR/pose, whichever is highest. "Something is changing." | Any of EAR, MAR, pose partially sustained |
| **L2** | Drowsy / Warning | Any **one** counter **fully tripped** (reaches its existing `*_CONSECUTIVE_FRAMES` threshold) — this is today's existing "DANGER" trip point, relabelled. **Yawn and pose alone can never exceed L2** (mirrors DOM-000). | EAR fully sustained, OR MAR fully sustained, OR pose fully sustained, OR 2+ counters simultaneously building (≥L1 each) |
| **L3** | Danger / Critical | **EAR counter only**, sustained **beyond** L2 for a further critical window (proposed: `EAR_CONSECUTIVE_FRAMES_L3 = 2× EAR_CONSECUTIVE_FRAMES`, i.e. eyes closed roughly twice as long as the L2 trip point) | Eye-closure only — never MAR/pose alone, per DOM-000 |

This keeps the existing threshold constants untouched (no retuning of EAR/MAR/pose sensitivity),
adds one new constant (`EAR_CONSECUTIVE_FRAMES_L3`), and adds a "building" band under the existing
trip points for L1. **Open question for approval: is the 0.4× ratio and 2× L3 multiplier
reasonable, or should these be independently tunable?** (flagged again in §7).

### 3.3 Obstruction (mask/sunglasses) — not a numbered level

Face obstruction (`obstruction_detector.py`) is handled as a **distinct "SENSOR FAULT" state**, not
folded into L1-L3 — it means *the system cannot see the driver*, which is categorically different
from *the driver is drowsy*. Visually it reuses the L2 (orange/warning) treatment plus a distinct
"CAMERA BLOCKED" label and icon, so it's obviously not a drowsiness reading.

---

## 4. Visual design — real instrument-cluster layout

```
┌──────────────────────────────────────────────────────────────────┐
│   ⛽ 3/4     ≡≡≡ MONITORING              🌡 22°C      09:41       │  ← top indicator strip
├──────────────────────────────────────────────────────────────────┤
│                                                                    │
│              ╭────────────────────────────╮                       │
│           ╱     0    20   40    60    80     ╲                    │
│          │   km/h        ╲   │   ╱      100    │                  │
│          │                ╲  │  ╱               │   ← big analog   │
│          │                 ╲ │ ╱                │     arc speedo,  │
│          │                  ╲│╱                 │     needle/arc   │
│          │                 62                   │     sweep        │
│           ╲               km/h                 ╱                  │
│              ╰────────────────────────────╯                       │
│                                                                    │
├──────────────────────────────────────────────────────────────────┤
│         ▲  ▲  ▲   (turn signals / seatbelt / high-beam, dim/off)  │  ← indicator icon row
└──────────────────────────────────────────────────────────────────┘
```

This is the **entire steady-state page** — no camera, no face box, no EAR/MAR numbers anywhere.
The drowsiness alert (L1/L2/L3) is not a permanent widget on this layout; it's a **transient
overlay layer** that appears on top of the cluster when triggered and disappears when clear,
exactly like a warning lamp/chime in a real car rather than a permanently-displayed gauge:

- **L1 (advisory)** — a slim amber strip slides down from the top edge, over the indicator strip,
  for ~2.5 s: a small warning-triangle icon + short text (`"Stay focused"` / `"Eyes on the road"`),
  **no sound**, speedometer keeps its normal white/gold face. Auto-dismisses. Mirrors spec 09's
  "amber banner, soft beep" (sound optional, see open question in §6.2).
- **L2 (warning)** — the speedometer face itself tints orange (needle/arc/rim glow), a
  **persistent** orange banner stays up while the condition holds (doesn't auto-dismiss), plus a
  repeating soft beep. Matches spec 09's "red overlay + loud alarm" intent, one notch down from L3.
- **L3 (danger)** — the **whole screen** flashes red (~2 Hz, semi-transparent red wash over the
  entire cluster including the speedo), a large centered icon + text replaces the top strip
  (`⚠ DROWSINESS DETECTED — PULL OVER`), continuous faster alarm tone. Stays until the underlying
  condition clears — no manual "ack" button in this prototype (no input device wired up; unlike
  `dms-ap`'s ACK_REFRACTORY concept over CAN).

### 4.1 Speedometer data source (needs your input, see §6.1)

`AppLab/` has no CAN link and no GPS/speed sensor of its own — there is no real "vehicle speed" to
read on a standalone UNO Q demo unit. Proposed default: **a simulated speed value**, generated
board-side (smooth random-walk between e.g. 0–120 km/h, occasionally easing toward 0 to look like
stop-and-go traffic), sent through the same `/status.json` feed as the alert level — this is the
same "simulate mode" idea `specs/09-ui-dashboard-spec.md` already defines for the desktop bridge
(`--mode simulate`), just generated locally instead of over CAN. If real telemetry becomes
available later (phone GPS via a paired app, OBD-II dongle, etc.) only the data source changes —
the dashboard rendering code doesn't need to know the difference.

- **Theme**: near-black background (`#0B0E14`-ish, not pure black — matches automotive OLED cluster
  night themes), navy dial face, white/gold needle and tick marks in the L0 idle state, amber/
  orange/red used **only** for the L1/L2/L3 alert layer (never as decoration elsewhere), gold
  accent reserved for the wordmark/branding only.

---

## 5. Technical approach (for the later implementation step)

- `webui.py` gains one more route, `GET /status.json`, returning **only** what the dashboard needs
  — no detection internals: `{"level": "L2", "speed_kmh": 62, "warning": "EYE_CLOSURE"}`. Same
  dependency-free `http.server` pattern already used for `/stream.mjpg`, a second in-memory
  "latest value" cache guarded by the existing lock. `warning` is an internal reason code only used
  to pick which short phrase the L1/L2 banner shows (`"Stay focused"` vs `"Eyes on the road"`) — the
  *numbers* behind it (EAR/MAR/pitch/yaw) never leave the Python process.
- `_INDEX_HTML` stops being a camera-viewer stub and becomes the instrument-cluster page: inline
  `<style>` + inline SVG for the speedometer arc/needle (no canvas library, no external CSS
  framework — single self-contained file, same philosophy as today) + a small vanilla-JS poller
  that fetches `/status.json` every ~200 ms, animates the needle/arc toward the new speed, and
  toggles CSS classes for the current alert level. **No `<img>`/MJPEG element on this page at all.**
- `webui.py` keeps serving `/stream.mjpg` unchanged (useful for bench debugging with a real
  browser), it's just not linked from `/` anymore — reachable directly at that URL if needed, not
  part of the redesigned experience.
- `main.py`'s `cv2.putText`/`cv2.rectangle` overlay drawing is **deleted outright** (not just
  restyled) — with no camera view in the product UI, there's no longer any reason to draw on the
  frames at all; this also removes per-frame drawing cost.
- `drowsiness.py` gains the L1/L2/L3 classifier described in §3.2, returning `level` (an enum-like
  str) instead of today's `status_text`/`status_color`. A small new module or inline generator adds
  the simulated `speed_kmh` value described in §4.1 and feeds it into the same status payload.

No new pip dependencies anywhere in this plan.

---

## 6. Open questions for approval

1. **§4.1 speed source** — OK to go with a **simulated** speed value for the demo (no real sensor
   exists on this standalone prototype), or do you have a real source in mind (phone GPS app,
   OBD-II dongle, manual slider for the demo booth) I should design around instead?
2. **§3.2 thresholds** — is "L1 = 40% of the way to today's trip point" and "L3 = 2× today's EAR
   trip point" the right feel, or do you want to eyeball/tune these against real footage first?
3. **Sound** — OK to add WebAudio beep/alarm tones (browser-only, no extra files needed — can
   synthesize a tone), or should L1-L3 stay visual-only for the demo?
4. **L3 auto-clear vs. requiring an explicit dismiss** — spec 09's desktop UI requires "clear from
   UNO Q or timeout" for its L3. This prototype has no ack input; is auto-clear-on-recovery
   acceptable, or do you want a tap-to-acknowledge zone on the page itself?
5. **Secondary gauges** — keep the layout to just the speedometer + top indicator strip (as drawn
   in §4), or also add a secondary gauge (RPM-style) / odometer like spec 09's desktop cluster has?
   Kept minimal by default since this is a phone/small-screen demo page, not a full dashboard.

Once these are answered (or you approve the defaults as written), implementation proceeds as a
separate step touching `webui.py`, `main.py`, and `drowsiness.py` only.

---

End of spec.
