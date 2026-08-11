# Summary of 09 — UI Dashboard Spec + Developer Guide

Source: [specs/09-ui-dashboard-spec.md](../../specs/09-ui-dashboard-spec.md),
[specs/09-ui-developer-guide.md](../../specs/09-ui-developer-guide.md)

## What this is
A desktop app that simulates a **vehicle instrument cluster** (speedometer, secondary gauge,
odometer, temperature, indicator icons) for demoing to judges. **Purely a demo/UI tool, not a
safety controller** — the VCS MCU remains the single source of truth for safety-critical actuation.

## Architecture
```
bridge (Rust binary)  <--WebSocket (MessagePack)-->  UI (Electron + React)
 - reads CAN adapters                                  - renders dials/gauges
 - reads UNO Q serial                                  - alert overlay + sound
 - replay / simulate
```
- 3 selectable modes from the dev panel: `live`, `replay`, `simulate`.
- WebSocket defaults to binding `localhost` (security), default port 8888.
- Uses **MessagePack** at runtime (not JSON) for compactness/speed; JSON in the spec is
  documentation only.

## The 2 main message types
- `can_signal`: periodic, decoded signal values (e.g. `vehicle.speed_kmh`, `vehicle.rpm`).
- `uno_alert`: high-priority event from the UNO Q — has `code`, `level` (0-3), `message`,
  `actions` (e.g. `["buzzer","vibrate","fan"]`).

## Signal → widget mapping
`vehicle.speed_kmh`→speedometer, `vehicle.rpm`→secondary gauge, `vehicle.odo_km`→odometer,
`vehicle.temp_c`→temperature indicator, booleans→top icon strip. Mapping defined in
`config/signal_map.yaml`, ideally generated from `shared/icd/icd.yaml` to keep one source of
truth (same principle as DEV-002 in spec 02).

## Alert behaviour by level
- L1: amber banner, soft beep.
- L2: red overlay, loud alarm + flash.
- L3: full-screen modal, repeating alarm — only clears on explicit clear from UNO Q or timeout.
- Multiple simultaneous alerts → show the highest level; others retained in the alert log in the dev panel.

## Performance requirements
- Alert render latency from UNO Q: target ≤50ms, worst-case ≤100ms.
- Smooth dial animation, default 20Hz, scaled by `devicePixelRatio`.
- Use a **Web Worker** to decode MessagePack, forward via `postMessage` to avoid blocking the main thread.
- `uno_alert` goes through a high-priority path and **must never be dropped** even under CAN
  backpressure; the bridge uses bounded queues for CAN, dropping oldest frames when saturated —
  but the alert queue avoids dropping.

## Layout & accessibility
- Two themes (light/dark). SVG vector icons. Speed number font 48-72px.
- High-contrast mode, large text, disable-flash options for accessibility.

## Suggested repo layout (Developer Guide)
```
bridge/   # Rust: reads CAN + UNO Q serial, WebSocket server
ui/       # Electron + React (TypeScript)
  src/components, src/canvas (dial rendering), src/workers (message decoding)
config/   # ui_config.yaml, signal_map.yaml
examples/ # sample replay/alert .msgpack files
```
- Bridge CLI: `--mode live|replay|simulate`, `--ws-port`, `--replay-file`.
- Suggested Rust crates: `tokio`, `tungstenite`/`warp`, `socketcan` (Linux CAN).
- Latency measurement: each `can_signal` carries a `ts` (bridge timestamp); UI computes
  `latency_ms = Date.now() - ts`.
- Testing: React component tests (`@testing-library/react`), integration tests using
  `bridge --simulate` + Puppeteer to check that `uno_alert` triggers DOM changes + audio playback.
- CI: JS via eslint+prettier+npm test; Rust via cargo fmt+clippy+cargo test.

## Typical dev workflow
1. Regenerate `signal_map.yaml` from `shared/icd/icd.yaml`.
2. `cargo run -- --mode simulate` (run the bridge).
3. `cd ui && npm run dev` (run the UI).
4. Verify dials update and alerts display correctly.

## Quick troubleshooting
- No alerts visible → check WebSocket logs, ensure message types are correct.
- Dials lagging → verify the worker is doing the decoding, consider lowering the UI update rate.
