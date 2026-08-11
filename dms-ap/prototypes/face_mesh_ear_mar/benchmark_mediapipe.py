import cv2
import mediapipe as mp
import time

# Khởi tạo MediaPipe (Dùng chế độ VIDEO để Benchmark cho chính xác)
base_options = mp.tasks.BaseOptions(model_asset_path='face_landmarker.task')
options = mp.tasks.vision.FaceLandmarkerOptions(
    base_options=base_options,
    running_mode=mp.tasks.vision.RunningMode.VIDEO
)
landmarker = mp.tasks.vision.FaceLandmarker.create_from_options(options)

cap = cv2.VideoCapture('test_video.mp4')
inference_times = []

while cap.isOpened():
    ret, frame = cap.read()
    if not ret: break
    
    rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb_frame)
    timestamp_ms = int(cap.get(cv2.CAP_PROP_POS_MSEC))

    # BẮT ĐẦU ĐO INFERENCE TIME
    start_time = time.perf_counter()
    
    result = landmarker.detect_for_video(mp_image, timestamp_ms)
    
    # KẾT THÚC ĐO
    end_time = time.perf_counter()
    
    # Tính thời gian bằng milliseconds
    infer_time_ms = (end_time - start_time) * 1000
    inference_times.append(infer_time_ms)

# Báo cáo kết quả
avg_inference = sum(inference_times) / len(inference_times)
print(f"[MediaPipe] Trung bình Inference Time: {avg_inference:.2f} ms")
print(f"[MediaPipe] FPS ước tính của AI: {1000 / avg_inference:.2f} FPS")