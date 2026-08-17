# SPDX-FileCopyrightText: Copyright (C) Arduino s.r.l. and/or its affiliated companies
#
# SPDX-License-Identifier: MPL-2.0

import threading
import time
from collections import deque
from datetime import datetime, UTC
from arduino.app_utils import App, Bridge, Logger
from arduino.app_peripherals.camera import Camera
from arduino.app_bricks.web_ui import WebUI
from arduino.app_bricks.video_objectdetection import VideoObjectDetection

logger = Logger("DMS_ObjectHunting")
ui = WebUI()
# fps=3 la tran cu rat thap so voi kha nang cua YOLOX Nano tren QRB2210.
# Nang len 8 lam gia tri khoi diem hop ly hon, nhung PHAI do lai FPS thuc
# te tren phan cung that va chinh lai -- khong co phan cung o day de
# benchmark truoc khi doi. Logic canh bao tren MCU (sketch.ino) da chuyen
# sang dem thoi gian bang millis() (thay vi dem so frame) nen an toan khi
# fps thay doi hoac frame bi drop (xem _bridge_worker ben duoi).
camera = Camera(fps=8)
detection_stream = VideoObjectDetection(camera=camera, debounce_sec=0.15, camera_preview=False)

# Cấu hình Per-Class Threshold Map (Threshold riêng cho từng nhãn/class trên cùng 1 mô hình)
# Xử lý thuần túy bằng Python (Built-in dict & conditional filtering, không dùng thư viện ngoài)
CLASS_THRESHOLDS = {
    "closed_eye": 0.35,  # Ngưỡng nhạy cho phát hiện nhắm mắt
    "yawning": 0.45,     # Ngưỡng cho phát hiện ngáp
    "open_eye": 0.55,    # Ngưỡng nghiêm ngặt hơn cho mở mắt
    "default": 0.40      # Ngưỡng mặc định cho các nhãn khác
}

def handle_override_class_th(sid, data):
    """
    Lắng nghe sự kiện tùy chỉnh threshold từng class từ Web UI:
    Data có thể là dict: {"label": "closed_eye", "threshold": 0.35}
    hoặc dict cập nhật nhiều class: {"closed_eye": 0.35, "yawning": 0.45, "default": 0.40}
    """
    if isinstance(data, dict):
        if "label" in data and "threshold" in data:
            label = str(data["label"])
            th = float(data["threshold"])
            CLASS_THRESHOLDS[label] = th
            logger.info(f"⚙️ [UI Config] Cập nhật Threshold riêng cho class '{label}' -> {th:.2f}")
        else:
            for k, v in data.items():
                try:
                    CLASS_THRESHOLDS[str(k)] = float(v)
                    logger.info(f"⚙️ [UI Config] Cập nhật Threshold riêng cho class '{k}' -> {float(v):.2f}")
                except (ValueError, TypeError):
                    pass
    elif isinstance(data, (int, float)):
        CLASS_THRESHOLDS["default"] = float(data)
        logger.info(f"⚙️ [UI Config] Cập nhật Default Threshold -> {float(data):.2f}")

ui.on_message("override_class_th", handle_override_class_th)

def handle_override_th_legacy(sid, threshold):
    handle_override_class_th(sid, {"label": "default", "threshold": threshold})
    # Gọi an toàn override_threshold nếu runner dịch vụ đã sẵn sàng
    try:
        detection_stream.override_threshold(float(threshold))
    except Exception as e:
        logger.warning(f"Chưa thể cập nhật runner threshold tầng dưới: {e}")

ui.on_message("override_th", handle_override_th_legacy)

# Biến toàn cục theo dõi thời gian và đếm khung hình
frame_id_counter = 0

# Cửa sổ trượt lưu timestamp của N frame gần nhất để tính FPS TRUNG BÌNH,
# thay vì FPS TỨC THỜI (1 / khoảng-cách-2-frame-gần-nhất). FPS tức thời rất
# nhiễu: chỉ cần 1 frame tới hơi sớm/hơi trễ hơn bình thường (do model,
# camera, hay GC jitter) là số hiển thị nhảy vọt dù tốc độ xử lý trung bình
# không đổi. Lấy trung bình trên nhiều mẫu gần nhất cho ra con số ổn định,
# phản ánh đúng thông lượng xử lý thật của pipeline.
_FPS_WINDOW = 15
_frame_timestamps = deque(maxlen=_FPS_WINDOW)

def _compute_fps() -> float:
    now = time.perf_counter()
    _frame_timestamps.append(now)
    if len(_frame_timestamps) < 2:
        return 0.0
    elapsed = _frame_timestamps[-1] - _frame_timestamps[0]
    if elapsed <= 0:
        return 0.0
    return (len(_frame_timestamps) - 1) / elapsed

# ======================================================
# Bridge.call() worker thread (KHÔNG chặn luồng inference)
# ======================================================
# Bridge.call("send_dms_bundle", ...) là RPC ĐỒNG BỘ: nó đợi hàm
# send_dms_bundle() bên MCU chạy xong (bao gồm cả các lệnh logBoth() in ra
# UART) mới trả về. Gọi trực tiếp trong send_detections_to_ui() -- vốn được
# gọi thẳng từ pipeline inference của VideoObjectDetection -- nghĩa là mỗi
# frame camera bị chặn chờ MCU + UART, giới hạn trần FPS thực tế của cả hệ
# thống xuống dưới cả tốc độ model.
#
# Đưa việc gọi Bridge.call() sang 1 thread nền: send_detections_to_ui() chỉ
# cập nhật "gói tin mới nhất cần gửi" rồi trả về ngay lập tức, không đợi
# MCU. Nếu 2 frame tới liên tiếp trước khi thread nền kịp gửi xong frame
# trước, gói cũ bị GHI ĐÈ (chỉ gửi gói mới nhất) thay vì xếp hàng vô hạn --
# việc này AN TOÀN với logic cảnh báo hiện tại vì MCU đã chuyển sang đếm
# thời gian bằng millis() (xem sketch.ino) thay vì đếm số lần được gọi.
#
# GIẢ ĐỊNH CHƯA KIỂM CHỨNG (không có phần cứng thật ở đây để test): đối
# tượng `Bridge` từ arduino.app_utils an toàn khi gọi .call() từ một thread
# khác thread chính của App.run(). Nếu không an toàn (vd nội bộ dùng
# asyncio event loop của thread chính), cách sửa nhanh là thay
# threading.Thread bằng cách đẩy vào asyncio.run_coroutine_threadsafe() hoặc
# một queue được App.run() tự bơm -- không cần đổi lại thiết kế tổng thể.
_bridge_lock = threading.Lock()
_bridge_pending = None  # tuple (frame_id, detections_str) hoặc None
_bridge_wakeup = threading.Event()

def _bridge_worker():
    global _bridge_pending
    while True:
        _bridge_wakeup.wait()
        with _bridge_lock:
            item = _bridge_pending
            _bridge_pending = None
            _bridge_wakeup.clear()
        if item is None:
            continue
        frame_id, detections_str = item
        try:
            Bridge.call("send_dms_bundle", frame_id, detections_str)
        except Exception as e:
            logger.error(f"Lỗi gửi Bundle xuống MCU (bridge worker thread): {e}")

threading.Thread(target=_bridge_worker, daemon=True, name="bridge-tx").start()

def _submit_bundle(frame_id: int, detections_str: str):
    global _bridge_pending
    with _bridge_lock:
        _bridge_pending = (frame_id, detections_str)
    _bridge_wakeup.set()

def get_confidence(obj) -> float:
    """Hàm an toàn lấy độ tin cậy từ đối tượng phát hiện"""
    if isinstance(obj, dict):
        return float(obj.get("confidence", 0.0))
    elif isinstance(obj, (int, float)):
        return float(obj)
    return 0.0

def get_bbox(obj):
    """Hàm an toàn lấy tọa độ Bounding Box từ đối tượng phát hiện (Trả về None nếu không có tọa độ thực)"""
    if isinstance(obj, dict):
        if "box" in obj and isinstance(obj["box"], list) and len(obj["box"]) == 4:
            return obj["box"]
        if "bbox" in obj and isinstance(obj["bbox"], list) and len(obj["bbox"]) == 4:
            return obj["bbox"]
        if "xmin" in obj and all(k in obj for k in ("xmin", "ymin", "xmax", "ymax")):
            return [obj.get("xmin"), obj.get("ymin"), obj.get("xmax"), obj.get("ymax")]
    return None

def send_detections_to_ui(detections: dict):
    """
    Hàm xử lý kết quả nhận diện từ YOLOX (Đã tối ưu giảm độ trễ):
    1. Tính toán FPS và hiệu năng xử lý.
    2. Lọc đối tượng theo Threshold riêng của từng Class (Per-Class Thresholds).
    3. Gom toàn bộ Bounding Box và FPS vào 1 gói tin WebSocket duy nhất gửi Web UI.
    4. Gom Class & Confidence vào 1 message duy nhất gửi MCU.
    """
    global frame_id_counter
    frame_id_counter += 1
    fps = _compute_fps()

    boxes_data = []
    detection_items = []

    if isinstance(detections, dict):
        for label, object_list in detections.items():
            str_label = str(label)
            # Lấy threshold riêng của class này (nếu không có thì lấy threshold default)
            required_th = CLASS_THRESHOLDS.get(str_label, CLASS_THRESHOLDS.get("default", 0.40))

            if isinstance(object_list, list):
                for obj in object_list:
                    accuracy = get_confidence(obj)
                    
                    # Lọc theo ngưỡng riêng của từng class
                    if accuracy >= required_th:
                        bbox = get_bbox(obj)
                        item = {
                            "label": str_label,
                            "confidence": round(accuracy, 2),
                            "threshold": round(required_th, 2)
                        }
                        if bbox is not None:
                            item["bbox"] = bbox
                        boxes_data.append(item)
                        detection_items.append(f"{str_label}:{accuracy:.2f}")

    # Gửi 1 message duy nhất cập nhật UI (Tránh lặp làm ngẽn đường truyền Socket)
    ui.send_message("frame_boxes", message={"boxes": boxes_data, "fps": round(fps, 1)})

    # Gom thành 1 message duy nhất gửi MCU
    all_detections_str = ",".join(detection_items) if detection_items else "none"

    logger.info(f"📤 [MPU Tx -> MCU] Frame #{frame_id_counter} | Detections: {all_detections_str}")

    # Không gọi Bridge.call() trực tiếp ở đây nữa (xem _bridge_worker phía
    # trên) -- chỉ giao gói tin cho thread nền rồi trả về ngay, để callback
    # inference này không bao giờ bị chặn chờ MCU/UART.
    _submit_bundle(frame_id_counter, all_detections_str)

# Callback tiếp nhận phản hồi hoặc thay đổi cấu hình từ UART ngoại vi (qua MCU)
def on_dms_config(raw_cmd: str):
    logger.info(f"📥 [MPU Config] Nhận lệnh từ UART ngoại vi: {raw_cmd}")

# Callback tiếp nhận Telemetry (Alert Level, Speed, Frame Cnt) từ MCU đẩy lên UI
def on_mcu_telemetry(alert_level: int, alert_code: int, speed: int, eye_frames: int, yawn_frames: int):
    telemetry_payload = {
        "alert_level": alert_level,
        "alert_code": alert_code,
        "speed": speed,
        "eye_frames": eye_frames,
        "yawn_frames": yawn_frames
    }
    ui.send_message("dms_telemetry", message=telemetry_payload)

# Lắng nghe sự kiện người dùng chạm màn hình UI để tắt cảnh báo -> gọi xuống MCU
def handle_dismiss_alert(sid, data=None):
    logger.info("🖐️ [MPU Rx UI] Chạm màn hình tắt cảnh báo -> Gửi lệnh dismiss_alert xuống MCU")
    try:
        Bridge.call("dismiss_alert")
    except Exception as e:
        logger.error(f"Lỗi gọi dismiss_alert xuống MCU: {e}")

# Callback tiếp nhận sự kiện tài xế mở mắt trở lại từ MCU
def on_driver_reopened(speed: int, closed_frames: int):
    logger.info(f"👁️ [MPU Rx <- MCU EVENT] TÀI XẾ ĐÃ MỞ MẮT TRỞ LẠI! (Nhắm mắt trước đó: {closed_frames} frames, Vận tốc: {speed} km/h)")
    ui.send_message("dms_event", message={
        "type": "EYES_REOPENED",
        "speed": speed,
        "closed_frames": closed_frames,
        "timestamp": datetime.now(UTC).isoformat()
    })

Bridge.provide("on_dms_config", on_dms_config)
Bridge.provide("on_mcu_telemetry", on_mcu_telemetry)
Bridge.provide("on_driver_reopened", on_driver_reopened)

ui.on_message("dismiss_alert", handle_dismiss_alert)

# Đăng ký hàm callback xử lý mỗi khi có kết quả phát hiện từ mô hình
detection_stream.on_detect_all(send_detections_to_ui)

App.run()