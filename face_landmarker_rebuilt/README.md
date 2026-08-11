# Dựng lại Face Landmarker mà không dùng file .task

## File .task thực chất là gì?

`face_landmarker.task` chỉ là một file **zip** đổi tên đuôi. Bên trong (đúng
với những gì bạn upload) gồm 4 file:

| File | Vai trò |
|---|---|
| `face_detector.tflite` | BlazeFace short-range — tìm bounding box khuôn mặt, input 128×128 |
| `face_landmarks_detector.tflite` | Hồi quy 478 điểm mốc 3D (x, y, z) trên khuôn mặt đã crop, input 256×256 |
| `face_blendshapes.tflite` | Từ 146 điểm mốc → 52 hệ số blendshape (biểu cảm) |
| `geometry_pipeline_metadata_landmarks.binarypb` | Mesh tam giác + mô hình khuôn mặt chuẩn để dựng hình học 3D (đây chính là nội dung trong `pipeline_structure.txt` bạn gửi — nó là bản dump dạng text của file `.binarypb` này) |

Khi bạn gọi `FaceLandmarker.create_from_options(...)` qua Tasks API, MediaPipe
tự động giải nén file `.task` này và chạy một C++ graph ẩn, gộp cả 4 bước
(detect → crop → landmark → geometry) làm một hộp đen.

Project này **bỏ qua Tasks API hoàn toàn**, tự viết lại từng bước bằng Python
+ `ai-edge-litert` (runtime TFLite thuần, kế thừa của `tflite-runtime` cũ) để
bạn thấy rõ và chỉnh sửa được từng khâu.

## Cấu trúc project

```
proj/
├── models/
│   ├── face_detector.tflite
│   ├── face_landmarks_detector.tflite
│   ├── face_blendshapes.tflite
│   └── geometry_pipeline_metadata_landmarks.binarypb
├── anchors.py        # Tự sinh 896 anchor box cho BlazeFace (thay cho SsdAnchorsCalculator ẩn trong graph gốc)
├── blaze_face.py      # Chạy face_detector.tflite: decode box, sigmoid score, NMS
├── face_landmark.py   # Crop theo bbox, chạy face_landmarks_detector.tflite, map 478 điểm về toạ độ ảnh gốc
├── main.py             # Vòng lặp webcam: detect -> landmark -> track -> vẽ -> đo latency
└── requirements.txt
```

## Cài đặt (chạy trên máy của bạn, PyCharm)

```bash
python -m venv venv
venv\Scripts\activate        # Windows
# hoặc: source venv/bin/activate   # macOS/Linux

pip install -r requirements.txt
```

Mở thư mục `proj/` này làm project trong PyCharm, đặt `main.py` làm entry
point, Run.

## Chạy

```bash
python main.py
```

- Cửa sổ webcam hiện lên, 478 điểm mốc màu xanh lá được vẽ đè lên khuôn mặt.
- Góc trên-trái hiển thị:
  - `FPS`
  - `Detect(BlazeFace)`: latency của bước phát hiện khuôn mặt (ms)
  - `Landmark(478pts)`: latency của bước hồi quy điểm mốc (ms)
  - `Total/frame`: tổng latency mỗi khung hình
- Phím `d`: bật/tắt "chạy detector mỗi frame" để so sánh latency với chế độ
  mặc định (chỉ chạy lại detector mỗi 30 frame, các frame còn lại dùng bbox
  suy ra từ landmark frame trước — đúng kỹ thuật tracking mà MediaPipe gốc
  cũng dùng để giảm chi phí).
- Phím `q`: thoát.

## Đào sâu thêm — vài hướng để bạn tự "chọc" tiếp vào project

1. **Thêm blendshape**: `face_blendshapes.tflite` nhận vào 146 điểm mốc con
   (một tập con của 478 điểm, theo đúng chỉ số mà MediaPipe định nghĩa) và
   trả về 52 hệ số biểu cảm (mắt nháy, miệng cười...). Bạn có thể viết thêm
   `blendshape.py` tương tự `face_landmark.py`.
2. **Dựng mesh 3D**: file `geometry_pipeline_metadata_landmarks.binarypb`
   (nội dung trong `pipeline_structure.txt`) chứa danh sách tam giác (các
   dòng field `4:`) để nối 468 điểm mốc thành lưới tam giác — dùng để vẽ mesh
   thay vì chỉ chấm điểm rời rạc.
3. **So sánh latency với bản gốc**: viết thêm một script dùng thẳng
   `mediapipe.tasks.python.vision.FaceLandmarker` với file `.task` gốc, đo
   latency rồi so với pipeline tự viết ở đây — bạn sẽ thấy bản C++ gốc nhanh
   hơn nhiều vì chạy native, không qua overhead của Python/OpenCV loop.
4. **Tối ưu**: hiện `main.py` decode box bằng NumPy thuần trên CPU; có thể
   dùng batching, giảm `DETECT_EVERY`, hoặc chạy landmark model ở độ phân
   giải thấp hơn để giảm latency hơn nữa.

## Chạy nhẹ hơn trên board yếu (Arduino UNO Q / RPi-class)

`main.py` có sẵn 4 tham số ở đầu file để tinh chỉnh cho phần cứng yếu hơn PC:

```python
NUM_THREADS = 4       # dat = so nhan CPU thuc te cua board (UNO Q: QRB2210 co 4 nhan A53)
DETECT_EVERY = 15     # cang lon -> cang it goi lai face_detector.tflite -> FPS cang cao
CAM_WIDTH = 320        # giam do phan giai webcam -> bot chi phi resize/convert mau
CAM_HEIGHT = 240
```

Cơ chế: thay vì chạy `face_detector.tflite` (bước tốn nhất) mỗi frame, chỉ chạy
lại mỗi `DETECT_EVERY` frame; các frame còn lại dùng bbox suy ra từ 478 điểm
mốc của frame ngay trước đó (`FaceLandmarkDetector.landmarks_to_bbox`). Đây
là kỹ thuật **tracking** — đánh đổi độ chính xác khi quay đầu nhanh lấy tốc
độ, giống cách MediaPipe bản gốc làm để chạy mượt trên thiết bị di động.

Lưu ý: **không thể lượng tử hoá (quantize) 2 file `.tflite` này** thành
bản int8 nhẹ hơn vì ta chỉ có file đã compile sẵn (float32), không có model
nguồn (SavedModel/Keras) để convert lại — nên các tham số trên là các đòn
bẩy khả thi duy nhất trong phạm vi những gì bạn có.

## Giới hạn cần biết
- Đây là bản tái dựng dựa trên việc đọc ngược input/output shape của các
  file `.tflite`, không phải mã nguồn gốc của Google — độ chính xác về
  landmark/latency có thể lệch nhẹ so với chạy qua Tasks API chính thức
  (đặc biệt là bước "rect transformation" và "smoothing" giữa các frame mà
  bản gốc có nhưng ở đây được đơn giản hoá).
- File `face_blendshapes.tflite` và `geometry_pipeline_metadata_landmarks.binarypb`
  chưa được dùng trong `main.py` (mới build tới bước landmark), bạn có thể
  tự mở rộng theo hướng dẫn ở trên.
