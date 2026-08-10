import cv2
import mediapipe as mp
import numpy as np
from scipy.spatial import distance as dist
import time
import os

# ==========================================
# CẤU HÌNH CÁC NGƯỠNG CẢNH BÁO
# ==========================================
# 1. Ngưỡng Buồn ngủ (Mắt)
EAR_THRESHOLD = 0.22
EAR_CONSECUTIVE_FRAMES = 15

# 2. Ngưỡng Ngáp (Miệng)
MAR_THRESHOLD = 0.55        # Tỷ lệ mở miệng (Tinh chỉnh 0.5 - 0.7)
MAR_CONSECUTIVE_FRAMES = 15 # Số khung hình liên tiếp há miệng (khoảng 1.5s - 2s)

# 3. Ngưỡng Mất tập trung (Tư thế đầu - Góc tính bằng độ)
YAW_THRESHOLD = 25          # Quay mặt sang trái/phải quá 25 độ
PITCH_THRESHOLD = 20        # Cúi gập đầu hoặc ngửa cổ quá 20 độ
POSE_CONSECUTIVE_FRAMES = 20

# Các biến đếm khung hình
ear_counter = 0
mar_counter = 0
pose_counter = 0

# ==========================================
# CHỈ MỤC LANDMARKS CỦA MEDIAPIPE
# ==========================================
LEFT_EYE = [362, 385, 387, 263, 373, 380]
RIGHT_EYE = [33, 160, 158, 133, 153, 144]
# Chỉ mục miệng (Môi trong) - Dùng 4 điểm này ĐỂ TÍNH TỶ LỆ MAR
MOUTH_MAR = [78, 308, 13, 14]

# SỬA LỖI: Danh sách các chỉ số điểm neo để VẼ ĐƯỜNG VIỀN MÔI (viền môi trong)
# Thứ tự các điểm này được sắp xếp theo vòng tròn
MOUTH_OUTLINE_INDICES = [
    78, 191, 80, 81, 82, 13, 312, 311, 310, 415, 
    308, 324, 318, 402, 317, 14, 87, 178, 88, 95
]

# ==========================================
# HÀM TRỢ GIÚP ĐỂ TRÍCH XUẤT TỌA ĐỘ
# ==========================================
def extract_coords(point_indices, landmarks, img_width, img_height):
    coords = []
    for idx in point_indices:
        point = landmarks[idx]
        x = int(point.x * img_width)
        y = int(point.y * img_height)
        coords.append((x, y))
    return coords

# ==========================================
# HÀM TÍNH TOÁN EAR & MAR
# ==========================================
# SỬA LỖI: Hàm này chỉ nhận tọa độ các điểm đã trích xuất để tính tỷ lệ
def calculate_eye_ear(eye_coords):
    # EAR: (Tổng 2 khoảng cách dọc) / (2 * Khoảng cách ngang)
    A = dist.euclidean(eye_coords[1], eye_coords[5])
    B = dist.euclidean(eye_coords[2], eye_coords[4])
    C = dist.euclidean(eye_coords[0], eye_coords[3])
    ear = (A + B) / (2.0 * C) if C > 0 else 0
    return ear

def calculate_mouth_mar(mouth_coords):
    # MAR: (Khoảng cách dọc) / (Khoảng cách ngang)
    # mouth_coords chứa 4 điểm từ MOUTH_MAR: 78, 308, 13, 14
    vertical_dist = dist.euclidean(mouth_coords[2], mouth_coords[3])
    horizontal_dist = dist.euclidean(mouth_coords[0], mouth_coords[1])
    mar = vertical_dist / horizontal_dist if horizontal_dist > 0 else 0
    return mar

# ==========================================
# CÀI ĐẶT MEDIAPIPE LIVE_STREAM
# ==========================================
BaseOptions = mp.tasks.BaseOptions
FaceLandmarker = mp.tasks.vision.FaceLandmarker
FaceLandmarkerOptions = mp.tasks.vision.FaceLandmarkerOptions
FaceLandmarkerResult = mp.tasks.vision.FaceLandmarkerResult
VisionRunningMode = mp.tasks.vision.RunningMode

model_path = 'face_landmarker.task'
if not os.path.exists(model_path):
    # Bạn cần tải file model này và đặt cùng thư mục với file Python
    print(f"[LỖI] Không tìm thấy file {model_path}. Tải từ: https://developers.google.com/mediapipe/solutions/vision/face_landmarker#python-task-api")
    exit()

# Biến toàn cục để lưu kết quả từ luồng Async
latest_result = None

# Hàm callback sẽ được gọi mỗi khi MediaPipe xử lý xong 1 frame
def result_callback(result: FaceLandmarkerResult, output_image: mp.Image, timestamp_ms: int):
    global latest_result
    latest_result = result

options = FaceLandmarkerOptions(
    base_options=BaseOptions(model_asset_path=model_path),
    running_mode=VisionRunningMode.LIVE_STREAM,
    result_callback=result_callback,
    output_facial_transformation_matrixes=True, # BẮT BUỘC BẬT để lấy tư thế đầu
    num_faces=1
)

landmarker = FaceLandmarker.create_from_options(options)

# ==========================================
# VÒNG LẶP CHÍNH
# ==========================================
cap = cv2.VideoCapture(0)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

print("[INFO] Đang chạy chế độ LIVE_STREAM. Nhấn 'q' để thoát...")

while cap.isOpened():
    ret, frame = cap.read()
    if not ret:
        print("[LỖI] Không thể đọc từ camera.")
        break

    frame = cv2.flip(frame, 1)
    img_h, img_w, _ = frame.shape
    
    # Chuẩn bị ảnh cho MediaPipe
    rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb_frame)
    timestamp_ms = int(time.time() * 1000)
    
    # Chạy inference bất đồng bộ (không làm khựng camera)
    landmarker.detect_async(mp_image, timestamp_ms)

    # Nếu có kết quả trả về từ callback
    if latest_result is not None and latest_result.face_landmarks:
        face_landmarks = latest_result.face_landmarks[0]
        
        # SỬA LỖI: Trích xuất tọa độ mắt và tính EAR
        left_eye_coords = extract_coords(LEFT_EYE, face_landmarks, img_w, img_h)
        right_eye_coords = extract_coords(RIGHT_EYE, face_landmarks, img_w, img_h)
        left_ear = calculate_eye_ear(left_eye_coords)
        right_ear = calculate_eye_ear(right_eye_coords)
        avg_ear = (left_ear + right_ear) / 2.0

        # SỬA LỖI: Trích xuất tọa độ miệng để tính MAR và trích xuất viền môi để vẽ
        mouth_mar_coords = extract_coords(MOUTH_MAR, face_landmarks, img_w, img_h)
        mar = calculate_mouth_mar(mouth_mar_coords)
        mouth_outline_coords = extract_coords(MOUTH_OUTLINE_INDICES, face_landmarks, img_w, img_h)

        # 3. TÍNH HEAD POSE (MẤT TẬP TRUNG)
        pitch, yaw, roll = 0, 0, 0
        if latest_result.facial_transformation_matrixes:
            # Lấy ma trận biến đổi 4x4
            matrix = latest_result.facial_transformation_matrixes[0]
            # Trích xuất ma trận xoay 3x3 (Góc trên bên trái)
            rotation_matrix = matrix[:3, :3]
            # Phân rã ma trận để lấy góc Euler (Pitch, Yaw, Roll)
            euler_angles, _, _, _, _, _ = cv2.RQDecomp3x3(rotation_matrix)
            pitch, yaw, roll = euler_angles

        # SỬA LỖI: Vẽ viền mắt và viền miệng chính xác
        cv2.polylines(frame, [np.array(left_eye_coords)], True, (0, 255, 0), 1)
        cv2.polylines(frame, [np.array(right_eye_coords)], True, (0, 255, 0), 1)
        # Đường vẽ miệng bây giờ sẽ ôm theo viền môi trong
        cv2.polylines(frame, [np.array(mouth_outline_coords)], True, (0, 255, 255), 1)

        # ==========================================
        # XỬ LÝ LOGIC CẢNH BÁO
        # ==========================================
        status_text = "TINH TAO"
        status_color = (0, 255, 0)
        warning_msg = ""

        # Logic nhắm mắt
        if avg_ear < EAR_THRESHOLD:
            ear_counter += 1
            if ear_counter >= EAR_CONSECUTIVE_FRAMES:
                warning_msg = "CẢNH BÁO: NGỦ GẬT!"
        else:
            ear_counter = 0

        # Logic ngáp
        if mar > MAR_THRESHOLD:
            mar_counter += 1
            if mar_counter >= MAR_CONSECUTIVE_FRAMES:
                warning_msg = "CẢNH BÁO: ĐANG NGÁP!"
        else:
            mar_counter = 0

        # Logic mất tập trung (Quay ngang / Cúi gập)
        if abs(yaw) > YAW_THRESHOLD or abs(pitch) > PITCH_THRESHOLD:
            pose_counter += 1
            if pose_counter >= POSE_CONSECUTIVE_FRAMES:
                warning_msg = "CẢNH BÁO: MẤT TẬP TRUNG!"
        else:
            pose_counter = 0

        # Nếu có cảnh báo nào được kích hoạt
        if warning_msg:
            status_text = "NGUY HIEM"
            status_color = (0, 0, 255)
            cv2.putText(frame, warning_msg, (10, 60), cv2.FONT_HERSHEY_SIMPLEX, 1.2, (0, 0, 255), 3)
            # Gửi tín hiệu UART đến STM32 tại đây (vd: serial_port.write(b'W'))

        # In thông số lên màn hình
        cv2.putText(frame, f"EAR: {avg_ear:.2f}", (450, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 0), 2)
        cv2.putText(frame, f"MAR: {mar:.2f}", (450, 55), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 0), 2)
        cv2.putText(frame, f"Pitch: {pitch:.0f} Yaw: {yaw:.0f}", (450, 80), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 0), 2)
        cv2.putText(frame, f"Trang thai: {status_text}", (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, status_color, 2)

    cv2.imshow("Driver Monitoring System", frame)
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cap.release()
landmarker.close()
cv2.destroyAllWindows()