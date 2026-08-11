# Tóm tắt 06 — Test Plan

Nguồn: [specs/06-test-plan.md](../specs/06-test-plan.md)

## Mục tiêu
**Không phải** để chứng minh DrowsyGuard hoạt động, mà để tìm ra **nó ngừng hoạt động ở đâu**
trước khi giám khảo tìm ra. 3 nguyên tắc:
1. Mọi claim phải có **số + phương pháp** đo (không nói "phát hiện microsleep đáng tin cậy" mà
   phải nói "TPR 0.96 (48/50 sự kiện), 0.7 báo giả/giờ, build X, run Y").
2. Đo **độ chính xác** và **độ trễ** tách biệt — gộp chung che giấu cái nào đang tệ hơn.
3. **Test đường lỗi ít nhất là kỹ như đường thành công** — rút cáp, che camera, sụt pin, kill
   Linux side... đây là những test mà 1 build chưa qua thì chưa được coi là demo-ready.

## 5 cấp độ test
| Cấp | Tên | Phạm vi | Chạy ở đâu | Gate |
|---|---|---|---|---|
| L1 | Unit | 1 module, không cần hardware | CI mỗi PR | Merge |
| L2 | Corpus replay | Domain+fusion logic vs recording đã annotate | CI mỗi PR | Merge |
| L3 | Node integration | 1 node, hardware thật, input mô phỏng | Bench, on-demand | Hằng ngày |
| L4 | System integration | Cả 2 node, CAN thật, motor thật | Bench rig | Hằng ngày |
| L5 | Acceptance | Kịch bản đầy đủ end-to-end | Demo rig | 1 lần trước demo |

**L2 là test giá trị cao nhất dự án** — vì `domains/`/`fusion/` là pure function (DEV-042), 1
bản ghi 30 phút đã annotate có thể replay qua đúng logic production trong <1 giây, không cần
camera/board, deterministic. Đây là cơ chế duy nhất ngăn "tune đến khi demo chạy được" âm thầm
phá false-alarm rate.

## Bench rig
- Logic analyser 8 kênh đo mọi mốc thời gian (inference done, CAN TX/RX, actuator change,
  control tick 100Hz, motor enable, CAN_H/L decode).
- **Latency đo bằng logic analyser, không dùng software timestamp giữa 2 node** — 2 clock
  không đồng bộ không đo được khoảng cách chéo node, scope thì đo được.
- GPIO marker phải compile vào cả debug **và** release build (đo overhead 1 lần, ghi lại).
- Mọi test có motor chạy trên **wheel stand** cho đến khi toàn bộ TC-SAF pass — 1 bug safe-stop
  trên sàn nhà là 1 chiếc xe lao vào vật gì đó.

## Phương pháp kích thích (stimulus)
| Phương pháp | Dùng cho | Lặp lại được? |
|---|---|---|
| Corpus replay vào pipeline | Domain/fusion/threshold logic | Hoàn toàn deterministic |
| Phát video lên màn hình cho camera | Toàn bộ optical path (exposure, IR, glare) | Lặp được trong dung sai ánh sáng |
| Người thật, kịch bản trực tiếp | UX/acceptance check | **Không lặp được — không bao giờ dùng để ra số liệu công bố** |
| Bơm frame CAN (`can_inject.py`) | Toàn bộ hành vi VCS, không cần camera | Hoàn toàn deterministic |

## Corpus (3 bộ, version-controlled qua git-lfs)
| Corpus | Nội dung | Mục tiêu thời lượng | Mục đích |
|---|---|---|---|
| **C-BASE** | Người tỉnh, hành vi bình thường (chớp mắt, nói, gương, uống nước...) | ≥60 phút | **Đo false-alarm** — corpus quyết định sản phẩm dùng được hay không |
| **C-DROWSY** | Buồn ngủ diễn/thật (nhắm mắt lâu, microsleep, ngáp, gật đầu) | ≥30 phút | Đo TPR |
| **C-ADVERSE** | Tối+IR, nắng chói, kính (trong/râm), che mặt, rung camera | ≥20 phút | Đo suy giảm & hành vi lỗi |

- Chia theo **subject** (không theo clip) thành tập tune và tập held-out — tránh tune/đánh giá
  trên cùng khuôn mặt cho ra số vô nghĩa với người ngoài phòng.
- Sự kiện mơ hồ (ngáp hay chỉ thở sâu?) phải có **2 người annotate độc lập**, báo cáo tỷ lệ
  không đồng thuận — detector không thể chính xác hơn ground truth của chính nó.

## Tiêu chí chấp nhận (acceptance criteria) — bảng cốt lõi
| # | Tiêu chí | Mục tiêu |
|---|---|---|
| AC-01 | Microsleep TPR (closure≥1.5s) | ≥ 0.95 |
| AC-02 | False alarm L1+ trên C-BASE | ≤ 1.0/giờ |
| AC-03 | Yawn F1 | ≥ 0.85 |
| AC-04 | Distraction TPR | ≥ 0.90 |
| AC-05 | Pipeline latency P95 | ≤ 200ms |
| AC-06 | FPS bền vững | ≥ 8 |
| AC-07 | Giữ FPS phút 30 vs phút 1 | ≥ 80% |
| AC-08 | Control-loop jitter | ≤ ±1ms |
| AC-09/10 | Timing `LINK_LOST`/safe-stop | 300ms / 1000ms (dung sai nhỏ) |
| AC-11 | Thời lượng safe-stop | 2.0s±0.1s |
| AC-12/13 | Trạng thái bất thường / bus-off trong 30 phút | 0 |
| AC-14 | ⚠️ ASSUMPTION còn mở | 0 |
| AC-15 | Áp suất âm | ≤85dB(A) |

- Nếu không đạt tiêu chí: **báo số thật** trong demo, không nắn tiêu chí cho khớp — 1 team nói
  "P95 của chúng tôi là 240ms, trên mục tiêu 200ms, đây là lý do" đáng tin hơn team nói mục
  tiêu như thể đó là kết quả đo.

## Entry/Exit criteria (rất thực dụng, dùng như checklist trước khi lên bench thật)
- **Vào L4:** cả 2 node build sạch từ main, CAN bring-up checklist xong, CRC vector pass, xe trên giá đỡ bánh.
- **Ra L4 / vào L5:** mọi TC-CAN/TC-SAF pass, chạy liên tục 30 phút không lỗi/bus-off, mọi
  benchmark trong spec 08 đã điền số thật, open-items ở spec 04/05 rỗng.
- **Ra L5 (demo-ready):** mọi acceptance criteria đạt (hoặc mỗi cái miss có số thật kèm giải
  thích), diễn tập demo 2 lần không cần operator can thiệp, **có video backup** phòng hardware hỏng ngày demo.

## Chính sách regression
- Bug nào tìm thấy ở L3+ phải sinh ra test case mới ở **cấp thấp nhất** có thể bắt được nó —
  nếu 1 bug ở bench đáng lẽ corpus replay bắt được, thì thiếu test L2 mới là defect thật sự.
- Suite L1+L2 phải chạy **dưới 3 phút** trên mỗi PR — chậm hơn sẽ bị bỏ qua khi áp lực deadline,
  đúng lúc cần nó nhất.
- Người viết module **không phải** người duy nhất test nó ở L4 (tránh cùng 1 mental model gây ra bug lẫn bỏ sót bug).
