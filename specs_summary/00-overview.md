# DrowsyGuard — Tóm tắt nhanh bộ specs

> Đọc file này trước. Mỗi file khác trong `specs_summary/` tóm tắt 1 file gốc trong `specs/`,
> đủ để nắm ý chính mà không cần đọc bản đầy đủ. Số liệu/ID quan trọng vẫn giữ nguyên để tra cứu.

## Dự án là gì

**DrowsyGuard** — thiết bị giám sát buồn ngủ tài xế chạy on-device (không cloud), phát hiện
qua camera rồi can thiệp vào một **xe mô phỏng thu nhỏ**. Làm cho **Qualcomm Future Makers —
Hack The Challenge 2026** (team ML_IoT_Love50), deadline demo **2026-08-16**. Rev 0.1 viết
**trước khi có phần cứng** (Arduino UNO Q còn đang cho mượn) — mọi số liệu hiệu năng là
**budget/target**, chưa phải **đo thực tế** (0 mục ✅ VERIFIED).

## Kiến trúc 2 node, nối qua CAN

```
DMS (Arduino UNO Q)                         VCS (NXP FRDM-MCXN947)
 ├─ DMS-AP: QRB2210, Linux, Python 3.13      ├─ FlexCAN0, motor control, safety
 │  → camera, MediaPipe face-landmark,       ├─ 4× TT gear motor (differential drive)
 │    fusion 3 domain → alert level L0-L3    ├─ buzzer/LED/rung/quạt/hazard
 └─ DMS-RT: STM32U585, gửi CAN               └─ speed cap + safe-stop theo alert_level
        │
        └──── CAN 2.0A classical, 500 kbit/s, 2 node, 120Ω 2 đầu ────┘
```

**Nguyên tắc tách trách nhiệm:** DMS quyết định "tài xế buồn ngủ mức nào", VCS quyết định
"xe làm gì với điều đó". VCS **không biết gì** về vision/AI — chỉ nhận 1 con số `alert_level`
qua CAN. Đường an toàn (CAN nhận → giới hạn tốc độ → safe-stop) chạy hoàn toàn trên MCU
VCS, không phụ thuộc Linux còn sống hay không.

## 3 miền phát hiện (domains) — trái tim của hệ thống

| Domain | Tín hiệu | Tối đa tự đạt | Ý nghĩa |
|---|---|---|---|
| **D1** Distraction | Nhìn lệch đường (yaw/pitch) | L2 | Hành vi — vẫn tỉnh nhưng không nhìn đường |
| **D2** Yawning | Ngáp (MAR + thời lượng) | L2 | Dự báo — sắp mệt, chưa nguy hiểm ngay |
| **D3** Eye closure | Nhắm mắt liên tục + PERCLOS | **L3** | Nguy hiểm tức thời — chỉ D3 được phép dừng xe |

4 mức cảnh báo: **L0 NORMAL → L1 EARLY → L2 DROWSY → L3 DANGER**. Leo thang cần *dwell time*
(giữ điều kiện liên tục đủ lâu) để chống báo giả; hạ mức khó hơn lên mức (hysteresis).
L3 chỉ thoát được bằng nút re-arm của operator, không tự hồi phục.

## Trạng thái hiện tại (quan trọng để hiểu ngữ cảnh)

- **VCS side đã xong thiết kế + code build sạch** (`vcs-mcxn947/`) nhưng **chưa flash/đo thật**.
- **DMS side (UNO Q) vẫn là ẩn số lớn nhất**: chưa xác nhận được FDCAN có ra chân header
  không (⚠️ ASM-01/OI-04-01) — rủi ro cao nhất toàn hệ thống, có fallback SPI-CAN (MCP2515).
- Toàn bộ ngưỡng ở spec 03 lấy từ **literature**, chưa tune trên corpus của team.
- Có thêm 1 phần **UI Dashboard** (spec 09) — app desktop Electron/React mô phỏng đồng hồ
  taplo xe, nhận dữ liệu qua WebSocket từ 1 bridge Rust đọc CAN. Đây là phần demo/trực quan,
  **không phải bộ điều khiển an toàn**.

## Bản đồ tài liệu (đọc theo thứ tự này nếu cần đào sâu)

| # | File | Nội dung | Tóm tắt |
|---|---|---|---|
| 01 | system-requirements | Yêu cầu toàn hệ thống (functional/perf/safety) | [xem](01-system-requirements.md) |
| 02 | development-standards | Quy tắc code, git, build, flash | [xem](02-development-standards.md) |
| 03 | drowsiness-domain-spec | Ngưỡng D1/D2/D3 + lý do từng con số | [xem](03-drowsiness-domain-spec.md) |
| 04 | interface-control-document | Layout CAN từng byte, timeout | [xem](04-interface-control-document.md) |
| 05 | vehicle-control-spec | Motor, alert actuator, safe-stop, failsafe | [xem](05-vehicle-control-spec.md) |
| 06 | test-plan | Chiến lược test, rig, corpus | [xem](06-test-plan.md) |
| 07 | test-cases | 130 test case cụ thể (0 đã chạy) | [xem](07-test-cases.md) |
| 08 | benchmark-log | Sổ ghi số đo thật (hiện toàn "_pending_") | [xem](08-benchmark-log.md) |
| 09 | ui-dashboard + dev-guide | App desktop mô phỏng taplo xe | [xem](09-ui-dashboard.md) |

## Những điểm dễ bị hỏi / dễ sai khi làm việc với repo này

1. **DEV-092**: khi thực tế khác spec → sửa spec ngay trong cùng PR đó, không để spec sai tồn tại.
   Đã xảy ra 2 lần (layout `dms-ap/`, và Python 3.13 thay vì 3.11).
2. **DEV-014**: sửa `safety.c`, `can_rx.c`, `fusion/`, `shared/icd/`, hay bất kỳ threshold nào
   → cần **2 approval**, 1 người không viết code đó.
3. **Ack (nút "tôi còn tỉnh")** không được xoá được D3 CRITICAL (SYS-FR-015) — tránh trở thành
   "defeat device". Ack cũng không reset bộ đếm ngáp/PERCLOS (chỉ tắt tiếng cảnh báo).
4. Mọi con số đo phải có `run ID` + thư mục artefact; không có artefact = không phải kết quả
   (BM-002). Không được build `+dirty` khi lấy số đo (DEV-072).
5. "3 giây = 100 mét" trong script hiện đang **sai** ở tốc độ 90 km/h thực (phải là 75 m) —
   xem [08](08-benchmark-log.md) mục 12, vẫn chưa fix tại thời điểm viết spec.
