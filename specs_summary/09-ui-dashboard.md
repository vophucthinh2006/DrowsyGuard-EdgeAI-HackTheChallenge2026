# Tóm tắt 09 — UI Dashboard Spec + Developer Guide

Nguồn: [specs/09-ui-dashboard-spec.md](../specs/09-ui-dashboard-spec.md),
[specs/09-ui-developer-guide.md](../specs/09-ui-developer-guide.md)

## Đây là gì
App desktop mô phỏng **cụm đồng hồ táp-lô xe** (speedometer, gauge phụ, odometer, nhiệt độ,
icon chỉ báo) để demo trực quan cho giám khảo. **Chỉ là công cụ demo/UI, không phải bộ điều
khiển an toàn** — VCS MCU vẫn là nguồn sự thật duy nhất cho actuation an toàn.

## Kiến trúc
```
bridge (Rust binary)  <--WebSocket (MessagePack)-->  UI (Electron + React)
 - đọc CAN adapter                                     - render dial/gauge
 - đọc serial UNO Q                                     - alert overlay + âm thanh
 - replay / simulate
```
- 3 chế độ chọn được từ dev panel: `live`, `replay`, `simulate`.
- WebSocket mặc định bind `localhost` (bảo mật), port mặc định 8888.
- Payload dùng **MessagePack** (không phải JSON) lúc runtime để nhẹ/nhanh; JSON trong spec chỉ
  để minh hoạ.

## 2 loại message chính
- `can_signal`: định kỳ, chứa signal đã decode (vd `vehicle.speed_kmh`, `vehicle.rpm`).
- `uno_alert`: sự kiện ưu tiên cao từ UNO Q — có `code`, `level` (0-3), `message`, `actions`
  (vd `["buzzer","vibrate","fan"]`).

## Mapping signal → widget
`vehicle.speed_kmh`→speedometer, `vehicle.rpm`→gauge phụ, `vehicle.odo_km`→odometer,
`vehicle.temp_c`→chỉ báo nhiệt độ, boolean→dải icon trên cùng. Mapping định nghĩa trong
`config/signal_map.yaml`, nên generate từ `shared/icd/icd.yaml` để giữ 1 nguồn sự thật duy nhất
(giống nguyên tắc DEV-002 ở spec 02).

## Hành vi cảnh báo theo level
- L1: banner vàng, tiếng bíp nhẹ.
- L2: overlay đỏ, còi to + nhấp nháy.
- L3: modal toàn màn hình, còi lặp lại — chỉ tắt khi có clear từ UNO Q hoặc timeout.
- Nhiều alert cùng lúc → hiển thị mức cao nhất, các alert khác lưu vào log ở dev panel.

## Yêu cầu hiệu năng
- Latency render alert từ UNO Q: mục tiêu ≤50ms, tệ nhất ≤100ms.
- Dial animation mượt, mặc định 20Hz, scale theo `devicePixelRatio`.
- Dùng **Web Worker** decode MessagePack, forward qua `postMessage` để không block main thread.
- `uno_alert` phải đi theo đường ưu tiên cao, **không được drop** dù CAN queue đang backpressure;
  bridge dùng bounded queue cho CAN, drop frame cũ nhất khi đầy — nhưng alert queue thì tránh drop.

## Layout & khả năng tiếp cận
- 2 theme (light/dark). Icon dạng SVG vector. Font số tốc độ 48-72px.
- Có chế độ high-contrast, chữ lớn, tắt hiệu ứng nhấp nháy (cho accessibility).

## Repo layout đề xuất (Developer Guide)
```
bridge/   # Rust: đọc CAN + UNO Q serial, WebSocket server
ui/       # Electron + React (TypeScript)
  src/components, src/canvas (render dial), src/workers (decode message)
config/   # ui_config.yaml, signal_map.yaml
examples/ # sample replay/alert .msgpack
```
- Bridge CLI: `--mode live|replay|simulate`, `--ws-port`, `--replay-file`.
- Bridge crate gợi ý: `tokio`, `tungstenite`/`warp`, `socketcan` (Linux CAN).
- Đo latency bằng cách mỗi `can_signal` mang theo `ts` (timestamp từ bridge); UI tính
  `latency_ms = Date.now() - ts`.
- Test: React component test (`@testing-library/react`), integration test dùng
  `bridge --simulate` + Puppeteer kiểm tra `uno_alert` → thay đổi DOM + phát âm thanh.
- CI: JS dùng eslint+prettier+npm test; Rust dùng cargo fmt+clippy+cargo test.

## Quy trình dev điển hình
1. Regenerate `signal_map.yaml` từ `shared/icd/icd.yaml`.
2. `cargo run -- --mode simulate` (chạy bridge).
3. `cd ui && npm run dev` (chạy UI).
4. Kiểm tra dial cập nhật + alert hiển thị đúng.

## Troubleshooting nhanh
- Không thấy alert → kiểm tra log WebSocket, đảm bảo message đúng type.
- Dial bị lag → kiểm tra worker có decode đúng chỗ không, cân nhắc giảm tần suất update UI.
