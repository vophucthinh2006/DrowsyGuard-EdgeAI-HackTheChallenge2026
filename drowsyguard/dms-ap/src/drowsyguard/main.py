import cv2
import time
import signal
import sys
import yaml
from inference.backend import InferenceBackend
from types import AlertLevel

# Global flag để quản lý tắt hệ thống an toàn
is_running = True

def handle_sigterm(signum, frame):
    global is_running
    print("\n2026-08-10T12:00:00Z INFO system event=shutdown_initiated")
    # Gửi lệnh Alert_Level = L0, calib_done = 0 xuống MCU để nhả phanh xe (DEV-045)
    # rpc.send(level=L0, calib=0)
    is_running = False

def main():
    signal.signal(signal.SIGINT, handle_sigterm)
    signal.signal(signal.SIGTERM, handle_sigterm)

    # 1. Load cấu hình SSOT (DEV-043)
    with open("../../config/thresholds.yaml", "r") as f:
        config = yaml.safe_load(f)

    # 2. Khởi tạo Backend và kết nối
    backend = InferenceBackend(model_path="../../models/face_landmarker.task")
    cap = cv2.VideoCapture(0)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

    print("2026-08-10T12:00:00Z INFO system event=boot_complete version=0.1.0")

    while is_running and cap.isOpened():
        ret, frame = cap.read()
        if not ret:
            break
        
        frame = cv2.flip(frame, 1)
        timestamp_ms = int(time.time() * 1000)

        # 3. Inference
        result = backend.process_frame(frame, timestamp_ms)

        # 4. Truyền dữ liệu vào Domains D1, D2, D3 (Sẽ implement ở bước sau)
        # d1_state = process_d1(result, timestamp_ms, config['d1_distraction'])
        # d2_state = process_d2(result, timestamp_ms, config['d2_yawning'])
        # alert_level = fusion_layer(d1, d2, d3)
        
        # 5. [Giả lập] Gửi RPC xuống C++
        # rpc.write(alert_level)

        # Draw Debug (Theo yêu cầu giữ lại mesh của bạn)
        debug_frame = backend.draw_debug_mesh(frame, result)
        cv2.imshow("DMS Debug View", debug_frame)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    # Clean up
    cap.release()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()