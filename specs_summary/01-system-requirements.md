# Tóm tắt 01 — System Requirements Specification

Nguồn: [specs/01-system-requirements.md](../specs/01-system-requirements.md)

## Phạm vi
- **Trong scope:** phát hiện trạng thái tài xế real-time on-device từ 1 camera; fusion 3 domain
  → 4 mức cảnh báo; CAN link đến VCS; xe mô phỏng 4 động cơ phản ứng theo mức cảnh báo; cảnh báo
  vật lý leo thang (âm thanh/đèn/rung); rig đo đạc.
- **Ngoài scope:** không đụng powertrain/CAN xe thật, không claim đạt chuẩn ISO 26262/ASIL,
  không cloud/fleet dashboard, không nhận diện danh tính tài xế.
- Câu bắt buộc nói mỗi khi demo: *"đây là demonstrator nghiên cứu, xe là mô phỏng, chưa qualify
  cho xe thật."*

## Kiến trúc (SYS-AR)
- Đúng 2 node thông minh (DMS, VCS) nối 1 đoạn CAN.
- DMS làm toàn bộ vision/inference/fusion; VCS **không** xử lý ảnh, không cần biết cách tính ra
  alert level — chỉ 1 điểm khớp nối: con số level.
- Đường an toàn (CAN nhận → giới hạn tốc độ → safe-stop) chạy hoàn toàn trên MCU VCS, độc lập
  với việc Linux có phản hồi hay không.
- Không ảnh/video/landmark nào được rời khỏi DMS — chỉ metric tổng hợp qua CAN.
- Hệ thống phải chạy được **hoàn toàn offline** (không Wi-Fi/cellular).
- Mọi state cá nhân tài xế bị xoá khi tắt nguồn.

## Chức năng chính (SYS-FR)
- Camera ≥10 FPS; phát hiện landmark mặt + head pose; suy ra mắt/miệng/hiện diện mặt.
- Mỗi detection có confidence score; dưới ngưỡng → coi là "không thấy", không phải "bằng chứng âm tính".
- Hoạt động cả ban ngày (RGB) và tối (IR) — cờ `night_mode`.
- Mất mặt quá lâu → `SENSOR_LOST`, báo như 1 **fault**, khác hẳn báo buồn ngủ.
- 3 domain D1/D2/D3 → fusion → L0-L3. Không mức nào trên L0 được raise chỉ từ 1 frame — luôn cần dwell time.
- Hạ mức có hysteresis (điều kiện hạ phải yếu hơn về thời gian so với điều kiện lên).
- Có nút "tôi còn tỉnh" (ack) xoá L1/L2; **không thể** xoá D3 CRITICAL (tránh defeat device).
- Leo thang đơn điệu L0→L1→L2→L3, trừ D3 CRITICAL được nhảy thẳng vào L3 từ bất kỳ đâu.
- Đèn 3 màu hiển thị mức hiện tại (xanh/vàng/đỏ/đỏ nhấp nháy = fault).
- VCS lái vi sai 2 kênh (trái/phải), giới hạn tốc độ theo alert level, L3 → safe-stop có ramp.
- Sau safe-stop, xe ở `STOPPED` và **không tự resume** — cần operator re-arm tường minh.
- Cả 2 node in banner boot (version, git SHA, timestamp); log mọi chuyển mức; log không chứa ảnh/PII.

## Hiệu năng (SYS-PR) — các con số cần nhớ
| Mục | Ngưỡng |
|---|---|
| Latency end-to-end (P95) | ≤ **200 ms** (budget nội bộ 160ms, có 40ms margin) |
| Frame-rate quantisation | tính riêng, không giấu vào latency |
| Inference rate bền vững | ≥ **8 FPS** (mục tiêu 10) trong bất kỳ cửa sổ 60s |
| Thermal throttle 30 phút | FPS phút 30 ≥ 80% FPS phút 1 |
| CAN bus load | ≤ 5% ở 500 kbit/s |
| VCS control loop | 100 Hz, jitter ≤ ±1 ms |
| Safe-stop deceleration | 2.0s ± 0.1s |
| DMS-AP RAM | ≤ 512 MB RSS; VCS free ≥20% RAM & flash |

Bảng phân bổ budget latency (tổng 160ms, margin 40ms): capture+convert 25ms, pre-process 10ms,
**inference 80ms** (phần lớn nhất), post+fusion 10ms, AP→RT handoff 10ms, CAN 5ms, VCS actuate 20ms.

## Interface & Safety (SYS-IR / SYS-SR)
- CAN 2.0A, 11-bit ID, 500 kbit/s, sample point 87.5%, termination 120Ω 2 đầu.
- Mọi frame định kỳ có seq counter 4-bit + CRC8; nhận sai → discard.
- Mất CAN link phải phát hiện trong **300 ms**.
- Cả 2 node 3.3V logic, không được nối 5V trực tiếp.
- VCS khởi động luôn ở trạng thái disarm, cần lệnh arm tường minh.
- Mất CAN input = fault, không bao giờ = "tài xế tỉnh táo". Hướng failsafe luôn là giảm tốc.
- Watchdog phần cứng trên VCS.
- Có **nút emergency-stop vật lý** ngắt trực tiếp motor driver, độc lập firmware.
- Nguồn motor và nguồn logic cấp cầu chì riêng.
- Âm lượng cảnh báo giới hạn ≤85 dB(A) @1m; L1 là tiếng nhẹ, không được làm tài xế giật mình.
- **False-positive bị giới hạn cứng: ≤ 1 alert/giờ ở L1+ khi tài xế tỉnh táo** — đây là hard
  requirement, không phải mong muốn.

## Môi trường
- Nhiệt độ hoạt động 0–45°C.
- Chiếu sáng 5 lux (IR tối) đến 50,000 lux (nắng trực tiếp).
- Hoạt động được với kính cận trong; **kính râm làm D3 UNAVAILABLE tường minh** (`model_degraded`),
  không được âm thầm coi như vẫn đo được mắt.
- Cold start đến khi có alert level đầu tiên trên CAN: ≤ 30s.

## Rủi ro/giả định lớn nhất
- **ASM-01 (⚠️ chưa xác nhận):** STM32U585 trên UNO Q có FDCAN ra chân header hay không.
  Đây là **rủi ro cao nhất hệ thống**. Nếu không → dùng SPI CAN controller (MCP2515-class),
  tốn ~1 ngày bring-up, quyết định trong 24h sau khi có hardware.
- ASM-02: chưa đo dòng stall thật của 4 motor TT.
- ASM-03: ngưỡng lấy từ literature, chưa tune trên corpus riêng của team.
