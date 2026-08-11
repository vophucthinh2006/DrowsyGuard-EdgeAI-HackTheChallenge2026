# Tóm tắt 02 — Development & Deployment Standards

Nguồn: [specs/02-development-standards.md](../specs/02-development-standards.md)

## Vì sao tài liệu này tồn tại
4 người viết firmware cho 2 MCU khác nhau + 1 app Linux, trong 6 ngày, trong khi phần cứng đến
giữa chừng dự án. Rủi ro lớn nhất không phải "không viết được code" mà là "3 phần không khớp
nhau và không ai biết phần nào sai".

## Cấu trúc repo (monorepo) — điểm hay bị hiểu nhầm
```
drowsyguard/
├── specs/                # hợp đồng kỹ thuật
├── dms-ap/app/           # ĐƠN VỊ DUY NHẤT App Lab deploy lên UNO Q
│   ├── app.yaml
│   ├── python/           # chạy trên MPU (QRB2210/Linux) — Python 3.13
│   │   ├── main.py
│   │   ├── config/thresholds.yaml   # NGUỒN DUY NHẤT mọi số ngưỡng
│   │   ├── models/*.tflite
│   │   └── drowsyguard/{capture,inference,domains,fusion,link,telemetry}
│   └── sketch/           # chạy trên MCU STM32U585 — ĐÂY LÀ "DMS-RT", không phải project riêng
├── vcs-mcxn947/          # firmware FRDM-MCXN947 (MCUXpresso SDK, out-of-tree)
├── shared/icd/           # icd.yaml = nguồn sự thật duy nhất của CAN layout, generate ra .h/.py/.dbc
├── tools/ tests/ docs/benchmarks/
```
**Điểm quan trọng:** Không có project top-level `dms-rt/` riêng như ban đầu tưởng — App Lab yêu
cầu 1 folder `app/` chứa cả Python (MPU) lẫn sketch (MCU), deploy như 1 đơn vị. Đây là bài học
thực tế (spec đã tự sửa 2 lần — xem DEV-092 bên dưới).

- `icd.yaml` là nguồn duy nhất viết CAN message layout; header C, module Python, file `.dbc`
  đều được **generate** ra, sửa tay file generate là bug. CI fail nếu generate lại ra diff.

## Git / review
- Trunk-based, `main` luôn build và flash được. Branch ngắn (<1 ngày): `feat/`, `fix/`, `spec/`, `test/`.
- Conventional Commits + `Refs: SYS-FR-012, DOM-D3-004` trailer để giữ traceability.
- Squash-merge, tối thiểu 1 approval.
- **2 approval bắt buộc** (1 người không viết code) khi đổi: `safety.c`/`can_rx.c` (failsafe/timeout),
  `fusion/` (thang cảnh báo), `shared/icd/` (interface), bất kỳ threshold nào trong `thresholds.yaml`.
- Không commit thẳng vào main, không force-push, không merge build đỏ.

## Coding standards — Firmware (C11)
- Flags nghiêm: `-Wall -Wextra -Werror -Wshadow -Wconversion ...`
- Sau init: **không dynamic allocation** (không malloc/new/VLA), buffer static, size compile-time.
- Chỉ dùng fixed-width types (`uint8_t`...) trong interface/struct.
- Naming convention rõ ràng theo bảng (module_verb_noun, s_ prefix cho file-scope var, global var bị cấm).
- ISR: ≤50µs, không printf/blocking/float/allocation, chỉ capture data + post queue/flag.
- Không magic number — mọi ngưỡng nằm trong `dg_config.h` (generate từ `thresholds.yaml`).
- State machine = `switch` tường minh trên enum, có `default:` log + vào safe state.
- MISRA-C:2012 chỉ là guideline có chọn lọc (không claim full compliance).
- **Không dùng floating point trong VCS control loop** — chỉ integer/fixed-point (Q16.16).

### Bài học nền tảng MCXN947 (rất cụ thể, dễ tra cứu khi debug)
- SDK driver là opt-in qua `prj.conf` — thiếu `CONFIG_MCUX_COMPONENT_driver.<x>=y` → lỗi
  **undefined reference lúc link**, không phải lỗi compile.
- Clock gating thủ công (`CLOCK_EnableClock`) — thiếu clock thì không báo lỗi, chỉ đọc về toàn 0.
- `debug_console_lite`'s `PRINTF` **không parse `%lu`** — in ra chữ "lu" theo nghĩa đen. Dùng `%u`
  + cast `(unsigned int)`.
- Out-of-tree app: `PROJECT_BOARD_PORT_PATH` phải là **relative path**, absolute path resolve sai
  và không tìm thấy `pin_mux.c`.

## Coding standards — App (Python 3.13, không phải 3.11)
- `black` (line 100) + `ruff`; `mypy --strict` trên `domains/` và `fusion/`.
- **`fusion/` và `domains/` phải pure**: nhận timestamp observation vào, trả state ra, không I/O,
  không đọc đồng hồ nội bộ → cho phép replay corpus 30 phút qua đúng logic production trong <1s,
  deterministic, chạy trong CI.
- Không hard-code threshold trong code — luôn load từ `thresholds.yaml`.
- Inference backend đứng sau interface `InferenceBackend`, có bản `ReplayBackend` đọc detection
  có sẵn từ corpus file → test được mọi thứ phía trên mà không cần camera/accelerator.
- `SIGTERM` phải shutdown sạch: publish `level=L0`, `calib_done=0`, disarm VCS.

## Threshold — nguồn sự thật duy nhất
- `thresholds.yaml` là nguồn duy nhất cho mọi số trong spec 03. CI chạy
  `tools/check_thresholds.py` đối chiếu bảng normative trong spec 03 với file này — lệch là fail build.
- Mỗi threshold phải có field `rationale:` — số không có lý do thì không bảo vệ được trước giám khảo.

## Logging
- 1 dòng/sự kiện, key=value, machine-parseable, có timestamp ISO8601.
- Levels: ERROR/WARN/INFO/DEBUG (DEBUG tắt ở release).
- **Không log ảnh, toạ độ landmark** — kể cả debug build.
- MCU: không log từ ISR, phải rate-limit.
- Mỗi benchmark run log vào `docs/benchmarks/<run-id>/` kèm git SHA, SHA-256 threshold, môi trường.

## Build & Flash
- Mỗi node có `build.sh` build từ sạch, không cần IDE.
- Firmware in ra version + git SHA (+`dirty` nếu tree không sạch) + timestamp khi boot.
- Build `+dirty` **không được dùng** để tạo số liệu ghi vào spec 08.
- CI thứ tự: ICD regen diff → threshold check → lint/type-check → unit test → corpus replay
  regression → build cả 2 firmware → cppcheck/clang-tidy.
- Flash VCS qua `pyocd` với `PROBE_UID` pin cứng (tránh prompt tương tác khi có nhiều probe).
- udev rule cho phép flash không cần sudo; nếu bắt buộc sudo thì phải giữ `env HOME=$HOME`.
- Mỗi lần deploy phải đọc lại boot banner để xác nhận đúng SHA đã deploy.

## Definition of Done (checklist, không có "gần xong")
Merged qua PR review + CI xanh · Refs ID trong commit · có unit test + corpus replay pass ·
threshold mới có rationale · đổi interface đã regenerate cả 2 bên · **verify trên hardware thật**
· nếu đổi số trong spec thì sửa spec trong cùng PR · đo đạc thì ghi vào spec 08 kèm artefact ·
không thêm ⚠️ ASSUMPTION mới mà không đăng ký vào Open Items.

## Change control
- Đổi threshold normative cần: giá trị cũ/mới, bằng chứng (run ID/corpus result), ảnh hưởng
  false-alarm rate, 2 approval.
- **DEV-092**: khi thực tế mâu thuẫn với spec → sửa spec ngay trong cùng PR phát hiện ra nó,
  không để lại số sai đã biết trong tài liệu.

## Anti-pattern bị cấm rõ ràng
Tune threshold đến khi demo chạy được · `sleep()` trong VCS control loop · bắt `except Exception`
rồi tiếp tục chạy (biến crash detector thành "tài xế ổn") · comment out test đang fail để merge ·
commit số liệu không kèm artefact · demo bằng build `+dirty` · thêm bản copy thứ 2 của 1 threshold "tạm thời".
