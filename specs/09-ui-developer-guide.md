
# 09 — UI Developer Guide (UI-DG-09)

**Document:** DG-SPEC-09-DG · Rev 0.1 · 2026-08-11 · DRAFT
**Applies to:** Developers building the DrowsyGuard Desktop Demo (UI + Bridge)

---

## 1. Purpose

This guide explains how to implement and integrate the desktop demo UI following the
UI specification. It includes recommended repository layout, configuration, runtime APIs,
implementation notes for the native bridge, testing guidance and performance measurement
instructions to meet the system latency targets.

## 2. Recommended repository layout

- `bridge/` — Rust service (CAN + UNO Q I/O, WebSocket server)
- `ui/` — Electron + React application (TypeScript)
  - `src/components` — reusable React components
  - `src/canvas` — canvas/WebGL rendering code for dials
  - `src/workers` — message decoding worker
- `config/` — `ui_config.yaml`, `signal_map.yaml`
- `examples/` — sample replay and alert files

## 3. Configuration and signal mapping

`config/signal_map.yaml` defines canonical signal names consumed by the UI. Example schema:

```yaml
signals:
  - name: vehicle.speed_kmh
    can_id: 0x100
    start_bit: 0
    length: 16
    scale: 0.01
    signed: false

  - name: vehicle.rpm
    can_id: 0x101
    start_bit: 0
    length: 16
    scale: 1

  - name: vehicle.turn_left
    can_id: 0x120
    start_bit: 0
    length: 1
    type: bool
```

Note: Generate `signal_map.yaml` from `shared/icd/icd.yaml` where possible to maintain a single
source of truth.

## 4. Runtime API (bridge → UI)

- WebSocket endpoint: `ws://localhost:<port>` (default 8888)
- Payload: MessagePack binary frames. Recognised `type` values: `can_signal`, `uno_alert`,
  `heartbeat`, `log`.

Message handling strategy:

- Use a Web Worker to open the WebSocket, decode MessagePack (e.g. `@msgpack/msgpack`), and
  `postMessage` parsed JSON objects to the UI main thread.
- The main thread stores latest signals in a small global store (React Context or Redux).

Example worker (concept):

```js
import { decode } from '@msgpack/msgpack';
const ws = new WebSocket('ws://localhost:8888');
ws.binaryType = 'arraybuffer';
ws.onmessage = (ev) => {
  const msg = decode(new Uint8Array(ev.data));
  postMessage(msg);
}
```

## 5. UNO Q alert handling rules

- Push `uno_alert` messages into a high-priority alert queue and render immediately.
- Preload audio assets and play using WebAudio or HTML `Audio` objects.
- Persist alerts in an in-app log with timestamps and provide CSV export.

## 6. Bridge implementation notes (Rust)

- CLI parameters: `--mode live|replay|simulate`, `--ws-port`, `--replay-file`.
- Maintain two internal queues:
  - `can_queue` — bounded; drop oldest frames when saturated.
  - `alert_queue` — high priority; avoid dropping UNO alerts if possible.
- Suggested crates: `tokio`, `tungstenite`/`warp` for WebSocket server, `socketcan` for Linux CAN,
  vendor SDKs for Windows if required.

Rust loop sketch:

```rust
loop {
  let frame = can.read()?;
  let signals = decode_frame(frame, &signal_map);
  ws_broadcast(Message::new_can_signal(ts, signals));
}
```

## 7. Testing

- Unit tests: React component tests with `@testing-library/react`.
- Integration tests: Run `bridge --simulate` and use Puppeteer to assert that `uno_alert`
  triggers DOM changes and audio playback.

## 8. Performance measurement

- Each `can_signal` MUST include a `ts` (bridge timestamp). The UI debug panel should echo `ts`.
- Latency measurement: `latency_ms = Date.now() - ts`.

## 9. CI and code quality

- JS: `eslint` + `prettier`; tests via `npm test`.
- Rust: `cargo fmt` + `clippy`; tests via `cargo test`.

## 10. Example developer workflow

1. Regenerate `signal_map.yaml` from `shared/icd/icd.yaml`.
2. Run bridge: `cargo run -- --mode simulate`.
3. Run UI dev server: `cd ui && npm run dev`.
4. Verify updates and alerts in the UI.

## 11. Troubleshooting

- If alerts not visible: inspect WebSocket logs and ensure messages are typed correctly.
- If dials lag: verify worker decode is used and consider reducing UI update rate.

## 12. Examples and deliverables

- `examples/replay/sample_replay.msgpack`
- `examples/alerts/sample_alerts.msgpack`

---

End of developer guide.

