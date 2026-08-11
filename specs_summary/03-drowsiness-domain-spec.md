# Tóm tắt 03 — Drowsiness Domain Specification

Nguồn: [specs/03-drowsiness-domain-spec.md](../specs/03-drowsiness-domain-spec.md)
*(Đây là tài liệu quan trọng nhất về mặt thuật toán — chứa mọi ngưỡng và lý do đằng sau nó)*

## Triết lý thiết kế (3 ý cốt lõi)
1. 3 tín hiệu **không cùng loại bằng chứng**: D1 = hành vi (đang tỉnh nhưng không nhìn đường),
   D2 = dự báo (sắp mệt), D3 = nguy hiểm tức thời (bất tỉnh ngay bây giờ). Vì vậy mỗi domain có
   detector, timer, và mức trần riêng — dùng chung 1 ngưỡng sẽ làm ngáp trở nên đáng báo động
   hoặc làm microsleep bị phát hiện trễ.
2. **Dwell time = hàng rào chống báo giả, đổi lấy độ trễ.** Mỗi ngưỡng luôn đi cùng thời gian
   phải giữ điều kiện đó — đây là cái phân biệt 1 cái chớp mắt với 1 cơn microsleep.
3. **Báo giả không miễn phí.** Tài xế bị đánh thức bởi báo giả sẽ học cách phớt lờ báo động —
   hệ thống "kêu sai" là tệ hơn không có hệ thống. Đây là lý do false-alarm rate là **hard
   requirement** (SYS-SR-008), không phải nguyện vọng.

## Bảng tổng quan domain
| ID | Domain | Loại tín hiệu | Đặc tính thời gian | Mức tối đa tự đạt |
|---|---|---|---|---|
| D1 | Distraction | Hành vi | Giây | L2 |
| D2 | Yawning | Dự báo | Phút | L2 |
| D3 | Eye closure | Nguy hiểm tức thời | Dưới giây → vài giây | **L3** |

**Chỉ D3 được phép đưa hệ thống lên L3** một mình. D1, D2 luôn bị chặn trần ở L2 dù nghiêm
trọng/lâu đến đâu — vì L3 = dừng xe, và chỉ "mắt nhắm nhiều giây" mới đủ căn cứ nói "tài xế
không điều khiển xe".

## D1 — Distraction
- `off_road` = |yaw|>30° HOẶC pitch xuống >20° HOẶC mặt có nhưng không thấy 2 mắt.
- Ngưỡng chính: **2000ms** liên tục → ACTIVE; **6000ms cộng dồn/12s** → SEVERE (hoặc 4s liên tục).
- Noise floor 600ms (bỏ qua liếc ngắn — kiểm tra gương/đồng hồ táp-lô bình thường).
- Clear dwell 3000ms (khó thoát cảnh báo hơn để vào).
- Khi bật xi-nhan mô phỏng: **chỉ hướng đang bật** được miễn trừ phát hiện yaw, pitch-down
  **không bao giờ** được miễn trừ.
- Nguồn gốc số liệu: chuẩn NHTSA 2s single-glance, nghiên cứu VTTI 100-Car (>2s off-road ~ tăng
  gấp đôi rủi ro va chạm). Ở 90km/h, 2 giây = 50 mét không nhìn đường.
- **Hạn chế đã biết:** D1 dùng head-pose làm proxy cho gaze — tài xế giữ đầu thẳng nhưng liếc mắt
  đi chỗ khác thì D1 không thấy được. Phải luôn công bố hạn chế này khi báo cáo hiệu năng D1.

## D2 — Yawning
- Đo mouth-open qua MAR (Mouth Aspect Ratio) > 0.60.
- Sự kiện ngáp hợp lệ: kéo dài **1500ms–12000ms**. Dưới 1500ms bị coi là nói chuyện/cười/ăn
  (nói chuyện chỉ mở miệng 150-250ms từng đợt); trên 12000ms bị coi là lỗi detection, discard +
  set `model_degraded`.
- **2 lần ngáp trong 2 phút** → ACTIVE; **3 lần trong 2 phút** (hoặc 1 lần ≥5000ms) → SEVERE.
- 1 lần ngáp duy nhất **không bao giờ** gây cảnh báo — đây là lựa chọn có chủ đích, hi sinh độ
  nhạy để đổi lấy uy tín (báo lần đầu tiên vì 1 cái ngáp là mất lòng tin tài xế ngay).
- Ack **không** reset bộ đếm ngáp — ack chỉ tắt tiếng, không làm tài xế bớt mệt.

## D3 — Eye closure (trái tim của sản phẩm)
Có 2 tín hiệu con độc lập, **cả hai đều bắt buộc**:

### (a) Nhắm mắt liên tục
| Ngưỡng | Giá trị | Ý nghĩa |
|---|---|---|
| ACTIVE | **800ms** | ~2× giới hạn trên của chớp mắt bình thường (100-400ms) — không thể bị kích hoạt bởi chớp mắt bình thường ở bất kỳ tốc độ nào |
| SEVERE | **1500ms** | Ngưỡng microsleep chuẩn y học/ô tô. 90km/h × 1.5s = 37.5m không ai lái |
| CRITICAL | **3000ms** | "Giả định không có tài xế". 90km/h × 3s = 100m. Đây là số headline của dự án |

- Thời gian tài xế **thực tế** cảm nhận = dwell + 1/FPS + pipeline latency:
  ACTIVE≈1.06s, SEVERE≈1.76s, CRITICAL≈3.26s. **Đây mới là số được phép công bố công khai**,
  không phải bare dwell time.
- Frame confidence thấp (<0.50) → accumulator **hold** (không reset, không tăng) — tránh cả 2
  lỗi: reset do noise huỷ mất microsleep thật, hoặc tăng do che camera fabricate ra 1 sự kiện giả.

### (b) PERCLOS (xu hướng)
- % thời gian mắt nhắm ≥80% trong cửa sổ trượt **60 giây**. Chuẩn fatigue được validate nhiều
  nhất trong literature (gốc FHWA/Wierwille).
- **8%** → ACTIVE-TREND, **15%** → SEVERE-TREND, hysteresis 2%.
- Cần ≥300 frame hợp lệ mới công bố giá trị, nếu chưa đủ → INVALID (0xFF trên CAN), không
  đóng góp vào state nào.
- **PERCLOS không bao giờ đẩy lên CRITICAL** — PERCLOS cao nghĩa là suy giảm trong 1 phút qua,
  không nghĩa là bất tỉnh ngay lúc này; chỉ tín hiệu (a) mới chứng minh được điều đó.

Hai tín hiệu bù cho nhau: (a) bắt sự kiện đơn lẻ nguy hiểm nhưng "mù" với suy giảm từ từ; (b)
bắt suy giảm dần nhưng phản ứng chậm (tới 60s) nên bỏ lỡ microsleep đầu tiên.

- State D3 hiệu dụng = **max** của (a) và (b).
- D3 CRITICAL **không thể xoá bằng ack** — chỉ tự hết khi mắt mở lại liên tục đủ 1000ms.
- Nếu phát hiện kính râm, hoặc confidence thấp >30% trong 3s gần nhất → D3 = **UNAVAILABLE**,
  set `model_degraded`, giữ ở L1 với chỉ báo riêng — **tuyệt đối không báo IDLE** trong tình
  huống này (rationale: "không thấy mắt" mà báo "mắt đang mở" là lỗi phần mềm nguy hiểm nhất
  hệ thống có thể mắc).

## Fusion — thang cảnh báo
| Level | Điều kiện vào |
|---|---|
| L0 | Mọi domain IDLE |
| L1 | Đúng 1 domain ACTIVE |
| L2 | Bất kỳ domain nào SEVERE, **HOẶC** ≥2 domain ACTIVE cùng lúc |
| L3 | D3 CRITICAL, **HOẶC** L2 kéo dài >10s không có ack |

- Quy tắc "2 domain ACTIVE = L2" là lý do kiến trúc 3-domain đáng giá: bằng chứng chéo cho phép
  từng ngưỡng riêng lẻ giữ mức bảo thủ (ít báo giả) mà hệ thống tổng thể vẫn nhạy.
- L2→L3 sau 10s không ack: đủ lâu để tài xế tỉnh nhấn nút, đủ ngắn để chưa đi quá 250m ở 90km/h.
- Ack xoá L1/L2 về L0, khởi động refractory 60s (chặn L1 tái vào, **không** chặn L2). Ack vô hiệu
  ở L3. Sau **3 lần ack** liên tiếp trong 10 phút không về L0 bền vững → `ack_saturated`, tắt
  refractory (chặn dùng nút ack như "nút tắt tiếng vĩnh viễn").
- Hạ mức: cần **mọi** domain IDLE liên tục 5s, và hạ **từng bước một** (không nhảy thẳng về L0).
- L3 **không tự hạ** — chỉ thoát bằng operator re-arm tường minh.

## Fault/degraded states (không phải trạng thái buồn ngủ, phải phân biệt rõ)
- `SENSOR_LOST`: mất mặt >3000ms khi đang armed → giống L1 nhưng **im lặng, không kêu báo buồn ngủ**.
- `MODEL_DEGRADED`: eye confidence thấp / D2 episode quá dài / phát hiện kính râm.
- `PIPELINE_SLOW`: FPS<5 trong >5s.
- Dwell luôn tính theo **thời gian thực (wall-clock)**, không theo số frame — dù FPS tụt xuống 3
  vẫn phải báo đúng ở 800ms thời gian thực, không phải sau N frame.
- Fault **không bao giờ** được âm thầm map về L0.

## Ghi chú quan trọng khác
- Mọi ngưỡng ở đây là **prior từ literature**, chưa phải đo thực nghiệm — phải tune lại trên
  corpus riêng của team trước khi accept.
- `D3_ACTIVE_MS/SEVERE_MS/CRITICAL_MS` không được giảm dưới giá trị hiện tại nếu không có bằng
  chứng đo phân bố blink-duration thực tế — đây là 3 số nhiều khả năng bị giám khảo chất vấn nhất.
- Citation R1-R6 (Wierwille/PERCLOS, VTTI 100-Car, NHTSA, blink duration, yawn duration) mới ở
  mức "cơ sở lý luận", **chưa verify với nguồn gốc** — phải làm trước khi công bố ra ngoài.
