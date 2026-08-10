import cv2
from ultralytics import YOLO
import time

# Load model YOLO (ví dụ bản Nano nhẹ nhất)
model = YOLO('yolov8n-drowsiness.pt') 

cap = cv2.VideoCapture('test_video.mp4')
inference_times = []

while cap.isOpened():
    ret, frame = cap.read()
    if not ret: break

    # BẮT ĐẦU ĐO INFERENCE TIME
    start_time = time.perf_counter()
    
    # YOLO inference (tắt visualize để đo thời gian AI thuần)
    results = model(frame, verbose=False) 
    
    # KẾT THÚC ĐO
    end_time = time.perf_counter()
    
    infer_time_ms = (end_time - start_time) * 1000
    inference_times.append(infer_time_ms)

# Báo cáo kết quả
avg_inference = sum(inference_times) / len(inference_times)
print(f"[YOLO] Trung bình Inference Time: {avg_inference:.2f} ms")
print(f"[YOLO] FPS ước tính của AI: {1000 / avg_inference:.2f} FPS")