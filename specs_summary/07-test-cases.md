# Tóm tắt 07 — Test Case Catalogue

Nguồn: [specs/07-test-cases.md](../specs/07-test-cases.md)

## Tình trạng
**130 test case, 0 đã chạy** (tất cả `N/R` — not run) tại Rev 0.1. Mỗi case có level (L1-L5),
requirement nó verify, và 1 pass criterion **không cần phán đoán chủ quan** — nếu 2 kỹ sư có
thể tranh cãi case pass hay không, case đó viết sai và cần sửa lại thay vì tranh luận.

## 9 nhóm test case (không liệt kê hết 130 case, chỉ nêu ý nghĩa từng nhóm + case đáng chú ý)

| Nhóm | Số case | Kiểm tra gì |
|---|---|---|
| **TC-ARC** | 6 | Kiến trúc & privacy — vd: rút camera vẫn lái được VCS qua CAN injection; kill DMS-AP thì VCS vẫn safe-stop đúng; CAN traffic không chứa ảnh/landmark nào |
| **TC-DOM** | 40 | Từng ngưỡng D1/D2/D3 — mỗi ngưỡng có case "vừa dưới ngưỡng thì không kích hoạt" và "đúng ngưỡng thì kích hoạt". Có case dùng corpus thật đo TPR/F1 (AC-01,03,04) |
| **TC-FUS** | 24 | Toàn bộ thang cảnh báo — kết hợp domain, escalate/de-escalate, ack, hysteresis, và verify actuation khớp bảng VEH-030 |
| **TC-CAN** | 15 | Vật lý CAN, CRC, encode/decode round-trip, timeout, ACK 3-lần, emergency-stop magic byte |
| **TC-SAF** | 17 | **Nhóm quyết định demo-ready hay không** — power-on an toàn, mất link, safe-stop không tự resume, watchdog, stall motor, undervoltage, e-stop hoạt động cả khi MCU treo trong debugger |
| **TC-VEH** | 15 | Motor: MIN_MOVE_DUTY, speed cap chính xác, ramp rate, safe-stop timing/deceleration, PWM frequency, jitter, stack headroom, âm lượng |
| **TC-PERF** | 12 | Latency P50/P95, FPS, thermal throttle, RAM/flash, cold-start ≤30s, overhead của GPIO marker |
| **TC-ROB** | 12 | Môi trường khắc nghiệt — nắng chói, tối+IR, kính, khẩu trang, mất khung hình, 2 mặt trong khung, rung chassis, brown-out, soak 3 giờ |
| **TC-DEV** | 6 | Quy trình — ICD regen không lệch, threshold khớp code, boot banner đúng, deploy đúng SHA |

## Case đáng nhớ nhất (thể hiện triết lý "an toàn thật, không phải an toàn trên giấy")
- **TC-SAF-013**: Nhấn e-stop vật lý **trong khi MCU đang bị halt bởi debugger** — phải vẫn dừng
  được motor. Đây là bài test chứng minh emergency-stop không phụ thuộc firmware.
- **TC-CAN-014**: Bơm `EMERGENCY_STOP` hợp lệ ở tốc độ đầy đủ → motor phải tắt trong ≤1 chu kỳ
  điều khiển (10ms).
- **TC-DOM-037 / TC-VEH-023 (SENSOR_LOST)**: che ống kính 5s → `SENSOR_LOST` ở 3s, đèn xanh
  dương nhấp nháy, **buzzer phải im lặng** — phân biệt rõ "che camera" với "buồn ngủ".
- **TC-SAF-009**: Vào L3, gửi L0 khi mới 300ms vào ramp → ramp phải **chạy hết**, không được
  tăng tốc lại giữa chừng.
- **TC-DOM-028/029**: closure có 1 frame confidence thấp xen giữa → accumulator phải **hold**,
  không reset không tăng; và cùng 1 sự kiện 800ms phải được phát hiện đúng lúc dù chạy ở 10FPS
  hay 3FPS (dwell tính theo thời gian thực, không theo số frame).
- **TC-FUS-016**: từ L2, mọi domain clear đủ 5.0s → chỉ được hạ xuống **L1**, không nhảy thẳng về L0.

## Traceability
Mỗi nhóm requirement (SYS-AR/FR/PR/IR/SR/ER, DOM-D1/D2/D3, DOM-FUS/FLT, CAN-*, VEH-*, DEV-*)
đều map tới 1 dải test case cụ thể. Có script `tools/check_traceability.py` chạy trước acceptance
gate để đảm bảo **không có requirement nào bị bỏ sót** không có case nào cover nó.
