# Tóm tắt 05 — Vehicle Control Specification (VCS · FRDM-MCXN947)

Nguồn: [specs/05-vehicle-control-spec.md](../specs/05-vehicle-control-spec.md)

## Vai trò
Nhận `alert_level` từ CAN → dịch thành hành vi xe + cảnh báo vật lý → fail-safe khi có sự cố.
Đây là 1 xe mô phỏng thu nhỏ, **không** kết nối và không được trình bày như đang kết nối xe
thật. Cái đáng giá thực sự là *state machine và hành vi lỗi*, motor chỉ là cách để "nhìn thấy"
nó hoạt động — giám khảo phải rút được cáp CAN và thấy xe phản ứng đúng.

## Phần cứng
- 4× motor TT ~1:48, lắp vi sai (trái FL+RL, phải FR+RR song song mỗi kênh).
- Driver H-bridge: **ưu tiên TB6612FNG** (logic 3.3V native, PWM 20kHz ngoài dải nghe được, sụt
  áp thấp ~0.5V) so với **L298N** dự phòng (logic 5V cần level-shift, PWM chỉ ~8kHz nghe được rõ
  — tiếng rít PWM cạnh tranh trực tiếp với buzzer cảnh báo).
- Pin mapping đã cross-check với schematic + SDK reference example, **build sạch**, chưa flash.
- Current sense chưa đấu dây (`OI-05-01`) — firmware coi đọc 0 là "chưa đấu dây", không phải fault.
- Cần tránh PIO1_3 (nối touch electrode qua R156).

## State machine (normative)
```
INIT → DISARMED → ARMED_IDLE → RUN ⇄ LIMITED → DECEL → STOPPED
                                              (any state) → ESTOP / FAULT
```
- Reset luôn vào `INIT`, motor disable, duty=0.
- `DISARMED→ARMED_IDLE` cần **cả 2**: operator arm input VÀ `flag_calib_done=1`.
- `STOPPED` chỉ thoát bằng operator re-arm tường minh, không bao giờ tự thoát theo alert level.
- `ESTOP` vào được từ **mọi** state trong 1 chu kỳ điều khiển, không ramp.
- Switch tường minh trên enum, có default → log + vào `FAULT`.

## Giới hạn tốc độ (speed governing)
```
duty_out = clamp(throttle_setpoint × speed_cap(level), 0, 100)
```
| Level | Speed cap | Lý do |
|---|---:|---|
| L0 | 100% | Không can thiệp |
| L1 | 80% | Giảm nhẹ, gần như không nhận ra — đủ rút ngắn quãng đường phanh, không đủ để cảm thấy bị phạt |
| L2 | 50% | Can thiệp rõ ràng — giảm nửa tốc độ ~ giảm 1/4 động năng |
| L3 | 0% (safe-stop) | Giả định không có tài xế |
| `LINK_LOST` | 30% | Suy giảm & chưa rõ — bảo thủ hơn mọi mức buồn ngủ đã biết dưới L3 |

- Thay đổi cap phải **rate-limited** (≤40%/s), không nhảy bậc — tránh xe giật/trượt bánh.
- Motor không quay dưới ~20% duty (ma sát tĩnh) → mapping có `MIN_MOVE_DUTY=25` (cần đo lại
  trên khung xe thật, thay đổi theo tải/pin).
- Trái/phải điều khiển độc lập (cho phép demo rẽ) nhưng cùng chịu 1 cap và 1 ramp limiter.

## Cảnh báo (alert actuation) — bảng normative đáng nhớ
| Level | Buzzer | LED | Rung | Fan | Hazard | Speed |
|---|---|---|---|---|---|---|
| L0 | im lặng | xanh | tắt | tắt | tắt | 100% |
| L1 | 2kHz, **2 xung rồi dừng** | vàng | tắt | tắt | tắt | 80% |
| L2 | 2.8kHz liên tục | đỏ | có | có | tắt | 50% |
| L3 | 3.2/2.4kHz xen kẽ liên tục | đỏ nhấp 4Hz | có | có | **nhấp 1Hz** | safe-stop |
| SENSOR_LOST | **im lặng** | xanh dương nhấp 1Hz | tắt | tắt | tắt | 80% |
| LINK_LOST | 1kHz ngắn/2s | vàng nhấp 2Hz | tắt | tắt | tắt | 30% |
| FAULT/ESTOP | 1kHz liên tục | đỏ nhấp 1Hz | tắt | tắt | bật | 0% |

- L1 chỉ kêu **2 xung rồi im** khi vẫn ở mức đó — kêu liên tục ở mức sớm nhất là cách nhanh
  nhất khiến tài xế ghét thiết bị.
- `SENSOR_LOST` phải **im lặng** và có màu riêng biệt (xanh dương) — che camera không phải tài
  xế buồn ngủ, kêu báo buồn ngủ vì lý do đó là sai và là cách nhanh nhất khiến camera bị dán băng dính.
- Âm lượng giới hạn ≤85dB(A)@1m — phải **đo thật**, không suy từ datasheet.
- Actuator patterns chạy từ 1 pattern engine non-blocking theo tick 100Hz — không `delay()`,
  không busy-wait.
- Mọi thay đổi level phải settle actuator trong ≤20ms từ lúc decode CAN frame.

## Safe-stop (normative)
| Pha | Thời lượng | Hành động |
|---|---|---|
| 1 — Ramp | 1500ms | Giảm duty tuyến tính về 0, đều cả 2 kênh |
| 2 — Brake | 500ms | Phanh chủ động (short cả 2 input H-bridge) |
| 3 — Hold | vô hạn | Disable motor, duty=0, hazard nhấp 1Hz, state `STOPPED` |

Tổng: **2.0s** từ lệnh đến đứng yên. Phanh phải **đối xứng** 2 kênh (bất đối xứng → xe xoay —
hành vi sai cần tránh trên nền tảng vi sai). Safe-stop đang chạy **không thể bị hủy** bởi 1 mức
alert thấp hơn đến giữa chừng — phải chạy hết (nếu mắt tài xế mở lại 200ms sau khi bắt đầu
dừng, xe vẫn vừa mới có 1 tài xế bất tỉnh — hoàn thành cú dừng là hành vi đúng và dễ đoán hơn
việc tăng tốc lại giữa chừng thao tác).

## Failsafe
- Theo đúng timeout ở spec 04 (`LINK_LOST` 300ms, safe-stop 1000ms).
- Mất `DMS_STATUS` **không bao giờ** = L0.
- WWDT 500ms, chỉ được service từ `control_task` 100Hz sau khi hoàn thành 1 vòng lặp đầy đủ
  (kick từ ISR timer chỉ chứng minh timer chạy, không chứng minh control loop còn ra quyết định).
- Reset do watchdog → vào `DISARMED`, set `fault_watchdog_reset` 5s, log ERROR, **không tự resume**.
- Dòng motor vượt `I_STALL_LIMIT` >500ms → `fault_driver`, disable driver, vào `FAULT`.
- Điện áp motor rail dưới `V_UNDERVOLT` >200ms → `fault_undervoltage`, vào `FAULT`.
- Mọi đường lỗi kết thúc ở trạng thái **không permissive hơn** trạng thái trước đó — không có
  transition trực tiếp nào từ fault về `RUN`.

## Task structure (FreeRTOS)
| Task | Priority | Chu kỳ | Trách nhiệm |
|---|---|---|---|
| `control_task` | cao nhất−1 | 10ms (100Hz) | state machine, ramp, duty, service watchdog |
| `can_rx_task` | cao nhất | event | decode, validate, refresh supervisor |
| `alert_task` | trung bình | 10ms | pattern engine buzzer/LED/haptics |
| `telemetry_task` | thấp | 100ms | gửi `VCS_STATUS`, console |

- Jitter `control_task` ≤±1ms. Duty chỉ được ghi **từ 1 task duy nhất** (`control_task`) — 1
  writer nghĩa là state actuator luôn giải thích được bằng state của 1 task, 2 writer = race
  condition tái hiện mỗi giờ 1 lần.

## Nguồn điện & dây điện
- Nguồn logic và motor cấp cầu chì riêng, motor có thể cô lập trong khi logic vẫn cấp điện để debug.
- 1 điểm nối đất sao duy nhất; dòng hồi motor không dùng chung dây với ground của CAN transceiver/ADC.
- Mọi tải cảm ứng (motor, relay, rung) phải có flyback diode — thiếu diode làm reset MCU và
  triệu chứng giống hệt firmware crash.
- Tụ bulk ≥1000µF ở đầu vào driver — 4 motor khởi động cùng lúc là load step tệ nhất hệ thống.
- E-stop vật lý ngắt **nguồn motor** (không phải tín hiệu logic), dùng công tắc mushroom-head chốt.

## Open items chưa đóng
Đo dòng stall motor thật · đo `MIN_MOVE_DUTY` trên khung tải thật · chốt TB6612FNG hay L298N ·
đo SPL 85dB(A) thật · đo profile giảm tốc thật (đã xong: xác nhận pin mapping không đụng độ).
