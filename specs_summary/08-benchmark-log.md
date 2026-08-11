# Tóm tắt 08 — Benchmark Log

Nguồn: [specs/08-benchmark-log.md](../specs/08-benchmark-log.md)

## Vai trò tài liệu
Đây là **sổ ghi số đo thật**, khác hẳn spec 01/06 (chứa budget/target). Tại Rev 0.1: **NO
MEASUREMENTS RECORDED YET** — mọi bảng đều `_pending_`. Đây là nơi số liệu sẽ được điền vào
khi có hardware thật, không phải nơi để copy con số dự đoán từ spec khác.

## Quy tắc ghi số (rất nghiêm ngặt — đáng nhớ)
- Chỉ ghi số **đã đo được**, không ghi target/budget/ước tính.
- Mỗi entry cần **run ID** (`BM-YYYY-MM-DD-NN`) + thư mục artefact `docs/benchmarks/<run-id>/`
  chứa raw capture, log, git SHA, SHA-256 của `thresholds.yaml`, environment record.
- **Không được** lấy số từ build `+dirty`.
- Latency/timing luôn báo **P50/P95/max**, không bao giờ báo mean (mean giấu đuôi phân phối —
  và đuôi phân phối chính là thứ 1 tài xế buồn ngủ thực sự trải nghiệm).
- Metric phát hiện luôn báo **theo cặp**: TPR **và** false-alarm rate cùng operating point — TPR
  đơn lẻ bị coi là kết quả không đầy đủ.
- Khi kết quả trượt mục tiêu: vẫn ghi **đúng như đo được** kèm nghi vấn nguyên nhân. Chạy lại
  đến khi ra số đẹp rồi chỉ ghi số đó = **fabrication dữ liệu**, bị cấm rõ ràng.
- Mỗi kết quả ghi tên người đo (không phải để đổ lỗi — để hỏi được người biết rig hôm đó ra sao).

## Cấu trúc các bảng (đều đang trống, sẽ điền khi có hardware)
Latency & timing (pipeline P50/P95/max, stage breakdown, time-to-alert thực tế người dùng cảm
nhận, control-loop jitter) · Inference throughput (FPS bền vững, hành vi nhiệt, RAM/flash
footprint) · Detection quality (operating point chính AC-01..04, chi tiết theo domain, **false
alarm breakdown theo nguyên nhân** — bảng này được ghi chú là "hữu ích nhất tài liệu" vì nó chỉ
ra threshold nào sai, không chỉ tổng số sai) · Interface (CAN vật lý, sức khoẻ bus 30 phút,
timeout behaviour) · Vehicle (drivetrain, speed governing, safe-stop, alerts) · Power · Scoreboard
15 acceptance criteria (**hiện tại 0/15 đã đo**) · Anomaly register (mọi quan sát bất thường,
kể cả cái sau này giải thích được — cái được ghi lại là cái được sửa) · **Numbers currently
quoted in external material**.

## Mục cần chú ý nhất: "Numbers currently quoted in external material"
Bảng đối chiếu mọi con số dùng trong pitch deck/script/submission với việc nó có backing đo
đạc hay không:
- "dưới 200ms end-to-end" → **Target**, chưa đo — phải nói "mục tiêu của chúng tôi là", thay
  bằng P95 đo thật khi có AC-05.
- "dưới 100ms trên MCU" (deck slide 11) → **Target**, budget thực tế trong spec là 20ms.
- **"3 giây = 100 mét" (script 0:08, 4:40) — LỖI SỐ HỌC ĐANG TỒN TẠI, CHƯA SỬA:** 100m trong 3s
  tương ứng 120km/h, nhưng script nói 90km/h → con số đúng phải là **75 mét**. Đây là kiểu lỗi
  "giám khảo cầm máy tính sẽ kiểm tra ra ngay" — cần sửa tốc độ thành 120km/h hoặc sửa số liệu
  thành 75m trước khi ghi hình/thuyết trình.
- "nano INT8 ở ≈N FPS" (deck slide 20) → **chỗ trống chưa điền**, phải điền số thật từ AC-06 hoặc xoá slide.
- "TRL 4" → tự đánh giá hợp lý ở thời điểm hiện tại, chỉ lên TRL 5-6 khi AC-01…15 đo được trên
  hardware thật.

**Việc cần làm trước khi quay bất kỳ video/demo nào: review lại đúng bảng này.**
