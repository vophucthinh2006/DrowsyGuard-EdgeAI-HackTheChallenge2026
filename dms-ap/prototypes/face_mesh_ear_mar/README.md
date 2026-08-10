# Hệ Thống Giám Sát Tài Xế (Driver Monitoring System)

Dự án này là một hệ thống mô phỏng giám sát trạng thái của tài xế theo thời gian thực (Real-time Driver Monitoring System). Hệ thống sử dụng Camera để thu thập hình ảnh và sử dụng trí tuệ nhân tạo (MediaPipe) để phát hiện các dấu hiệu nguy hiểm khi lái xe như:
1. **Ngủ gật (Drowsiness)**: Dựa vào độ nhắm/mở của mắt.
2. **Ngáp (Yawning)**: Dựa vào độ mở của miệng.
3. **Mất tập trung (Distraction)**: Dựa vào hướng quay của đầu (ngó lơ, cúi gập đầu).

---

## 1. Cơ Chế Hoạt Động Của Trí Tuệ Nhân Tạo (AI)

### 1.1. Xác định các điểm neo (Landmarks)
Các điểm neo được xác định dựa vào mô hình AI **MediaPipe Face Mesh** của Google.
- **Cách hoạt động**: Khi nạp file `face_landmarker.task`, hệ thống sử dụng kiến trúc mạng **BlazeFace** để phát hiện vùng chứa khuôn mặt. Sau đó, một mạng nơ-ron tích chập (CNN) 3D Face Mesh được sử dụng để hồi quy (regression) và dự đoán tọa độ không gian 3 chiều $(x, y, z)$ của **478 điểm** trên khuôn mặt.
- **Chỉ mục cố định**: Google đã lập bản đồ sẵn 478 điểm này. Do đó, điểm số 33 luôn là khóe mắt phải, và điểm 78 luôn là khóe miệng trái. Mã nguồn chỉ cần tra cứu đúng mảng chỉ mục (như `LEFT_EYE` hay `MOUTH_OUTLINE_INDICES`) để trích xuất dữ liệu.
- **Tính toàn vẹn (Holistic)**: Mô hình này tính toán và trả về toàn bộ 478 điểm (bao gồm cả cổ, mũi) trong mỗi lần quét. Hệ thống của chúng ta chỉ lọc ra các điểm liên quan đến mắt và miệng để xử lý, giúp tiết kiệm tài nguyên.

### 1.2. Vẽ và liên kết các điểm viền
Hệ thống sử dụng hàm vẽ hình học của thư viện OpenCV (`cv2.polylines`) để liên kết các điểm:
- **Trích xuất tọa độ**: MediaPipe trả về tọa độ chuẩn hóa từ $0.0$ đến $1.0$. Tọa độ này được chuyển thành pixel thực tế trên màn hình theo công thức: $X = x \times width$ và $Y = y \times height$.
- **Sắp xếp mảng**: Khi truyền mảng điểm vào, thuật toán của OpenCV sẽ vẽ các đường thẳng nối tuần tự các điểm trong mảng (vd: điểm 1 nối với điểm 2, điểm 2 nối với điểm 3...).
- **Đóng vòng (Closed loop)**: Việc thiết lập tham số `isClosed=True` giúp tự động vẽ thêm một nét nối từ điểm cuối cùng của mảng ngược về điểm đầu tiên, tạo thành một đa giác khép kín (như viền bao quanh mắt hoặc môi).

---

## 2. Thuật Toán, Phép Tính và Ngưỡng (Threshold)

Để tính toán kích thước, hệ thống sử dụng **Khoảng cách Euclidean (Euclidean Distance)** để đo độ dài đoạn thẳng giữa 2 điểm $P_1(x_1, y_1)$ và $P_2(x_2, y_2)$ trên mặt phẳng 2D:
$$d = \sqrt{(x_2 - x_1)^2 + (y_2 - y_1)^2}$$

### A. Phép tính EAR (Eye Aspect Ratio - Tỷ lệ khung hình mắt)
Dựa trên công thức từ bài báo khoa học của Soukupová và Čech. Mắt được cấu thành từ 6 điểm (gọi là $P_1$ đến $P_6$, trong đó $P_1, P_4$ là hai khóe mắt; các điểm còn lại thuộc mí trên và mí dưới):
$$EAR = \frac{\vert{}\vert{}P_2 - P_6\vert{}\vert{} + \vert{}\vert{}P_3 - P_5\vert{}\vert{}}{2 \times \vert{}\vert{}P_1 - P_4\vert{}\vert{}}$$
- **Threshold nhắm mắt (`EAR < 0.22`)**: Khi nhắm mắt, khoảng cách mí trên/dưới tiến về 0, làm EAR tụt xuống dưới 0.22. Kết hợp với việc đếm 15 frames liên tục (khoảng 1 giây) để hệ thống phân biệt được giữa việc chớp mắt sinh lý bình thường (rất nhanh) và ngủ gật.

### B. Phép tính MAR (Mouth Aspect Ratio - Tỷ lệ khung hình miệng)
Tương tự EAR, nhưng áp dụng cho viền môi trong:
$$MAR = \frac{\vert{}\vert{}P_{môi\_trên} - P_{môi\_dưới}\vert{}\vert{}}{\vert{}\vert{}P_{khóe\_trái} - P_{khóe\_phải}\vert{}\vert{}}$$
- **Threshold ngáp (`MAR > 0.55`)**: Khi ngáp, miệng mở rộng theo chiều dọc nhanh hơn chiều ngang, đẩy tỷ lệ MAR vượt mức 0.55. Yêu cầu 15 frames liên tục để tránh nhầm lẫn với việc tài xế đang nói chuyện bình thường.

### C. Thuật toán tính góc quay đầu (Head Pose Estimation)
Để xác định mức độ mất tập trung, hệ thống tính toán ma trận xoay từ `facial_transformation_matrixes` do MediaPipe cung cấp (đại diện cho vị trí và hướng khuôn mặt trong không gian 3D so với camera).
- **Trích xuất**: Code sẽ cắt lấy góc 3x3 phía trên cùng bên trái của ma trận 4x4 để thu được **Ma trận xoay (Rotation Matrix)**.
- **Thuật toán phân rã (RQ Decomposition)**: Hàm `cv2.RQDecomp3x3` (đại số tuyến tính) phân rã ma trận xoay thành các **góc Euler (đơn vị độ)**:
  - **Pitch (Trục X)**: Góc gật gù (cúi xuống / ngửa lên).
  - **Yaw (Trục Y)**: Góc quay trái / phải.
  - **Roll (Trục Z)**: Góc nghiêng đầu sang một bên vai.
- **Threshold mất tập trung**: 
  - `abs(yaw) > 25`: Quay đầu sang hai bên quá 25 độ.
  - `abs(pitch) > 20`: Cúi gập đầu (vd: xem điện thoại) hoặc ngửa cổ chợp mắt quá 20 độ.
  - Cảnh báo kích hoạt nếu kéo dài **20 frames liên tục** (dài hơn ngưỡng nhắm mắt một chút để cho phép tài xế có khoảng thời gian an toàn quay đầu quan sát gương chiếu hậu khi chuyển làn).

---

## 3. Sơ đồ luồng xử lý (Flowchart)

Sơ đồ dưới đây mô tả quá trình xử lý song song mỗi khung hình (Frame) của hệ thống:

```mermaid
graph TD;
    Start([Bắt đầu Camera]) --> ReadFrame[Đọc Khung Hình (Frame)]
    ReadFrame --> CheckFrame{Có Frame không?}
    CheckFrame -- Không --> Stop([Kết thúc])
    CheckFrame -- Có --> Preprocess[Chuyển đổi RGB & Trích xuất MP Image]
    
    Preprocess --> MPAsync[MediaPipe Inference Async]
    MPAsync --> Callback((Callback))
    Callback -. Cập nhật .-> LatestResult[(Latest Result)]
    
    Preprocess --> GetResult{Có Face Landmarks?}
    LatestResult -. Truy xuất .-> GetResult
    
    GetResult -- Không --> Display[Hiển thị Giao Diện]
    
    GetResult -- Có --> CalcEAR[Tính EAR (Mắt)]
    GetResult -- Có --> CalcMAR[Tính MAR (Miệng)]
    GetResult -- Có --> CalcPose[Tính Pitch/Yaw (Tư thế đầu)]
    
    CalcEAR --> CheckEAR{EAR < Ngưỡng?}
    CalcMAR --> CheckMAR{MAR > Ngưỡng?}
    CalcPose --> CheckPose{Góc > Ngưỡng?}
    
    CheckEAR -- Có --> AddEAR[Tăng đếm khung hình (Mắt)]
    CheckEAR -- Không --> ResetEAR[Reset đếm Mắt = 0]
    
    CheckMAR -- Có --> AddMAR[Tăng đếm khung hình (Miệng)]
    CheckMAR -- Không --> ResetMAR[Reset đếm Miệng = 0]
    
    CheckPose -- Có --> AddPose[Tăng đếm khung hình (Tư thế)]
    CheckPose -- Không --> ResetPose[Reset đếm Tư thế = 0]
    
    AddEAR --> LimitEAR{Quá số khung hình?}
    AddMAR --> LimitMAR{Quá số khung hình?}
    AddPose --> LimitPose{Quá số khung hình?}
    
    LimitEAR -- Có --> WarnDrowsy[Cảnh báo Ngủ gật]
    LimitMAR -- Có --> WarnYawn[Cảnh báo Đang ngáp]
    LimitPose -- Có --> WarnDistract[Cảnh báo Mất tập trung]
    
    WarnDrowsy --> Act[Cập nhật UI & Gửi lệnh UART/Còi]
    WarnYawn --> Act
    WarnDistract --> Act
    ResetEAR --> Act
    ResetMAR --> Act
    ResetPose --> Act
    LimitEAR -- Không --> Act
    LimitMAR -- Không --> Act
    LimitPose -- Không --> Act
    
    Act --> Display
    Display --> ReadFrame
```

## 4. Yêu cầu cài đặt
- Python 3.12 (hoặc tương thích)
- Các thư viện: `pip install -r requirement.txt` (yêu cầu `mediapipe<1.0.0`, `opencv-python`, `numpy`, `scipy`)
- Mô hình: Tải file `face_landmarker.task` đặt cùng thư mục dự án.

## 5. Cách chạy
Khởi chạy lệnh:
```bash
python realtime_drowsy.py
```
Nhấn phím `q` tại màn hình camera để thoát chương trình.
