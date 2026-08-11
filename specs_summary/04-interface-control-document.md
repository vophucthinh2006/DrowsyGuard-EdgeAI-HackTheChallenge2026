# Tóm tắt 04 — Interface Control Document (CAN)

Nguồn: [specs/04-interface-control-document.md](../specs/04-interface-control-document.md)

## Vật lý bus
- Classical CAN 2.0A (không dùng CAN FD dù cả 2 chip hỗ trợ — vì payload chỉ 8 byte, FD chỉ
  thêm độ phức tạp bit-timing không cần thiết).
- 500 kbit/s, sample point 87.5%, termination **120Ω mỗi đầu** (đo được 60Ω±5Ω khi tắt nguồn —
  bus chỉ có 1 hoặc 3 điểm termination sẽ "có vẻ chạy được" ở khoảng cách ngắn rồi lỗi ngắt quãng).
- 3.3V logic cả 2 phía. Bus ≤2m (bench), giới hạn lý thuyết 40m ở tốc độ này.
- **VCS side đã xác nhận:** FlexCAN0, PORT1_10/11, transceiver **on-board TJA1057GTK/3Z** —
  không cần linh kiện ngoài, đã build clean trong `vcs-mcxn947/`, chưa flash đo thật.
- **DMS side vẫn ⚠️ ASSUMPTION:** FDCAN1 trên STM32U585 có ra chân header UNO Q không — chưa xác nhận.

## Bảng message (ID thấp = ưu tiên cao hơn khi arbitration)
| ID | Tên | Hướng | DLC | Chu kỳ | Ghi chú |
|---|---|---|---|---|---|
| `0x080` | `EMERGENCY_STOP` | 2 chiều | 2 | event, ≤3 lần @10ms | Ưu tiên tuyệt đối |
| `0x100` | `DMS_STATUS` | DMS→VCS | 8 | **100ms** | Message an toàn quan trọng nhất |
| `0x101` | `DMS_METRICS` | DMS→VCS | 8 | 500ms | Chỉ để telemetry, không ảnh hưởng actuation |
| `0x200` | `VCS_STATUS` | VCS→DMS | 8 | **100ms** | Phản hồi trạng thái xe |
| `0x201` | `VCS_EVENT` | VCS→DMS | 2 | event | Ack, re-arm, e-stop |
| `0x700/0x701` | `DIAG_REQ/RESP` | | 8 | on request | Thấp nhất |

Tổng bus load ≈ **0.6%** của 500kbit/s (yêu cầu ≤5%) — dư địa rất lớn, đúng chủ đích: bus
không bao giờ được là lý do 1 message an toàn bị trễ.

## `0x100 DMS_STATUS` — message an toàn duy nhất VCS thực sự dùng để hành động
- Byte 0: `alert_level` (0-3) + `seq` 4-bit.
- Byte 1: state của D1/D2/D3 (2-bit mỗi cái) + `d3_avail` (available/degraded/unavailable).
- Byte 2: `perclos_pct` (255=invalid). Byte 3-4: `eye_closure_ms`. Byte 5: `face_conf_pct`.
- Byte 6: các cờ (ack_refractory, sensor_lost, model_degraded, night_mode, calib_done,
  pipeline_slow, ack_saturated).
- Byte 7: CRC-8 SAE-J1850 trên byte 0-6.
- **`alert_level` là trường DUY NHẤT VCS hành động theo** — mọi trường khác chỉ để hiển thị/log/chẩn đoán.
- Validate thứ tự: DLC==8 → CRC khớp → seq đúng tiếp theo. Sai bất kỳ bước nào → **discard, KHÔNG
  refresh timeout supervisor** (frame lỗi mà vẫn refresh watchdog còn nguy hiểm hơn mất frame —
  nó che giấu 1 link đang chết bằng data cũ).
- Cho đến khi `flag_calib_done=1`, VCS luôn ở trạng thái disarm bất kể `alert_level` là gì.

## Các message khác
- `0x200 VCS_STATUS`: trạng thái xe (INIT/DISARMED/ARMED_IDLE/RUN/LIMITED/DECEL/STOPPED/
  LINK_LOST/FAULT/ESTOP), speed cap, duty trái/phải + hướng, các cờ fault, indicator (dùng để
  D1 suy giảm gaze-suppression khi xi-nhan bật).
- `0x201 VCS_EVENT`: ack/re-arm/e-stop/indicator — gửi **3 lần cách nhau 10ms** cùng `event_seq`,
  bên nhận de-dup theo seq (vì event không có chu kỳ lặp lại tự nhiên để tự phục hồi — mất 1
  frame ACK nghĩa là tài xế bấm nút mà hệ thống lờ đi, chính là thứ phá huỷ lòng tin).
- `0x080 EMERGENCY_STOP`: có byte `magic=0x5A` để tránh 1 frame lỗi ngẫu nhiên ở ID ưu tiên cao
  nhất vô tình dừng xe. **Đây là đường tiện lợi, không phải đường an toàn chính thức** — an
  toàn thực sự vẫn là công tắc vật lý ngắt nguồn motor (hoạt động cả khi firmware treo).

## Timeout supervision — phần quan trọng nhất tài liệu này
| Mốc | Giá trị | Hành động |
|---|---|---|
| Chu kỳ nominal | 100ms | |
| Degrade (mất 3 chu kỳ) | **300ms** | Vào `LINK_LOST`, cap tốc độ 30%, cảnh báo amber |
| Safe-stop | **1000ms** | Thực hiện safe-stop đầy đủ |
| Phục hồi | 5 frame hợp lệ liên tiếp | Vào lại ở mức **mới nhận được**, không phải mức trước lỗi |

- **Bug kinh điển cần tránh:** không bao giờ được coi "không có `DMS_STATUS`" = "alert_level=L0".
  Giữ giá trị cũ mãi mãi làm 1 link chết trông y hệt 1 tài xế hoàn toàn tỉnh táo — lỗi này im
  lặng, qua mọi test chức năng, và chỉ lộ ra khi dây rút giữa lúc demo.
- Bus-off phải tự phục hồi và được đếm/log; 1 lần bus-off trong demo = demo không sạch.

## Open items (rủi ro cần theo dõi)
- **OI-04-01** (⚠️ cao nhất): FDCAN trên UNO Q có ra header không — quyết định trong 24h sau khi
  có board. Fallback: SPI CAN (MCP2515-class, +1ms latency, +1 ngày bring-up) rồi UART framed.
- OI-04-03/04/05: bit-timing, CRC-8 implementation, transceiver — **phía VCS đã resolve xong**,
  phía DMS vẫn open (chưa có code CRC bên DMS để cross-verify).

## Checklist bring-up (làm theo thứ tự, không nhảy cóc)
Đo bus resistance 60Ω±5Ω (nguồn tắt) → soi CAN_H/L bằng scope → 1 node phát vào bus có
termination không có node thứ 2 (xác nhận không có ACK là đúng) → đo bit time = 2.00µs±1% →
2 node cùng bus, error counter =0 trong 60s → CRC test vector khớp cả 2 build → dùng
`can_inject.py` lái VCS qua mọi alert_level không cần DMS → rút cáp giữa chừng, xác nhận
`LINK_LOST` ở 300ms và safe stop ở 1000ms bằng scope.
