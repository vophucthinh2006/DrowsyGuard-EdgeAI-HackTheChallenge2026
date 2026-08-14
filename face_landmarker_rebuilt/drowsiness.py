"""
Logic phat hien buon ngu / ngap / mat tap trung, port lai tu 2 file mau
ban gui (driver monitoring system dung mediapipe.tasks + facial_transformation
_matrixes). O day khong dung Tasks API / file .task, nen phan head-pose duoc
thay bang cv2.solvePnP (ky thuat pho bien, chi can 6 diem moc + mo hinh 3D
mat chuan gan dung, khong can toan bo geometry pipeline cua MediaPipe).

Dung: tao 1 DrowsinessMonitor(), moi frame goi monitor.update(landmarks, frame_shape)
voi landmarks la mang (478,3) tra ve tu FaceLandmarkDetector.predict().
"""
import numpy as np
import cv2

from landmark_regions import EAR_LEFT_EYE, EAR_RIGHT_EYE, MOUTH_MAR, HEAD_POSE_IDX

# ==== NGUONG CANH BAO (dieu chinh theo thuc te neu can) ====
EAR_THRESHOLD = 0.22
EAR_CONSECUTIVE_FRAMES = 15

MAR_THRESHOLD = 0.55
MAR_CONSECUTIVE_FRAMES = 15

YAW_THRESHOLD = 25      # do
PITCH_THRESHOLD = 20    # do
POSE_CONSECUTIVE_FRAMES = 20

# Mo hinh 3D gan dung cua 6 diem moc chuan (don vi tuy y, chi can dung ty le
# tuong doi giua cac diem) - thu tu phai khop voi HEAD_POSE_IDX trong
# landmark_regions.py: [mui, cam, khoe mat trai, khoe mat phai, khoe mieng trai, khoe mieng phai]
_MODEL_3D_POINTS = np.array([
    (0.0, 0.0, 0.0),        # mui (goc toa do)
    (0.0, -63.6, -12.5),    # cam
    (-43.3, 32.7, -26.0),   # khoe mat trai
    (43.3, 32.7, -26.0),    # khoe mat phai
    (-28.9, -28.9, -24.1),  # khoe mieng trai
    (28.9, -28.9, -24.1),   # khoe mieng phai
], dtype=np.float64)


def _euclidean(p1, p2):
    return float(np.hypot(p1[0] - p2[0], p1[1] - p2[1]))


def eye_aspect_ratio(coords):
    """coords: 6 diem (x,y) theo dung thu tu EAR_LEFT_EYE/EAR_RIGHT_EYE."""
    a = _euclidean(coords[1], coords[5])
    b = _euclidean(coords[2], coords[4])
    c = _euclidean(coords[0], coords[3])
    return (a + b) / (2.0 * c) if c > 0 else 0.0


def mouth_aspect_ratio(coords):
    """coords: 4 diem (x,y) theo dung thu tu MOUTH_MAR = [trai, phai, tren, duoi]."""
    vertical = _euclidean(coords[2], coords[3])
    horizontal = _euclidean(coords[0], coords[1])
    return vertical / horizontal if horizontal > 0 else 0.0


def estimate_head_pose(landmarks_px, frame_shape):
    """Uoc luong (pitch, yaw, roll) bang solvePnP tu 6 diem moc 2D.
    Day la phep xap xi (mo hinh 3D chuan hoa, khong phai model rieng cua
    tung khuon mat) - du dung de bat huong quay dau ro ret."""
    h, w = frame_shape[:2]
    image_points = np.array(
        [(landmarks_px[i][0], landmarks_px[i][1]) for i in HEAD_POSE_IDX],
        dtype=np.float64,
    )

    focal_length = w
    center = (w / 2.0, h / 2.0)
    camera_matrix = np.array([
        [focal_length, 0, center[0]],
        [0, focal_length, center[1]],
        [0, 0, 1],
    ], dtype=np.float64)
    dist_coeffs = np.zeros((4, 1))

    ok, rotation_vec, _ = cv2.solvePnP(
        _MODEL_3D_POINTS, image_points, camera_matrix, dist_coeffs,
        flags=cv2.SOLVEPNP_ITERATIVE,
    )
    if not ok:
        return 0.0, 0.0, 0.0

    rotation_mat, _ = cv2.Rodrigues(rotation_vec)
    euler_angles, _, _, _, _, _ = cv2.RQDecomp3x3(rotation_mat)
    pitch, yaw, roll = euler_angles

    # Chống lật góc 180 độ
    if pitch > 90:
        pitch = 180.0 - pitch
    elif pitch < -90:
        pitch = -180.0 - pitch

    return float(pitch), float(yaw), float(roll)


class DrowsinessMonitor:
    """Giu trang thai (counter) qua cac frame, tra ve thong tin de ve len man hinh."""

    def __init__(self):
        self.ear_counter = 0
        self.mar_counter = 0
        self.pose_counter = 0

    def update(self, landmarks_px, frame_shape):
        left_eye = [tuple(landmarks_px[i][:2]) for i in EAR_LEFT_EYE]
        right_eye = [tuple(landmarks_px[i][:2]) for i in EAR_RIGHT_EYE]
        mouth = [tuple(landmarks_px[i][:2]) for i in MOUTH_MAR]

        left_ear = eye_aspect_ratio(left_eye)
        right_ear = eye_aspect_ratio(right_eye)
        avg_ear = (left_ear + right_ear) / 2.0
        mar = mouth_aspect_ratio(mouth)
        pitch, yaw, roll = estimate_head_pose(landmarks_px, frame_shape)

        status_text = "TINH TAO"
        status_color = (0, 255, 0)
        warning_msg = ""

        if avg_ear < EAR_THRESHOLD:
            self.ear_counter += 1
            if self.ear_counter >= EAR_CONSECUTIVE_FRAMES:
                warning_msg = "CANH BAO: NGU GAT!"
        else:
            self.ear_counter = 0

        if mar > MAR_THRESHOLD:
            self.mar_counter += 1
            if self.mar_counter >= MAR_CONSECUTIVE_FRAMES:
                warning_msg = "CANH BAO: DANG NGAP!"
        else:
            self.mar_counter = 0

        if abs(yaw) > YAW_THRESHOLD or abs(pitch) > PITCH_THRESHOLD:
            self.pose_counter += 1
            if self.pose_counter >= POSE_CONSECUTIVE_FRAMES:
                warning_msg = "CANH BAO: MAT TAP TRUNG!"
        else:
            self.pose_counter = 0

        if warning_msg:
            status_text = "NGUY HIEM"
            status_color = (0, 0, 255)

        return {
            "ear": avg_ear,
            "mar": mar,
            "pitch": pitch,
            "yaw": yaw,
            "roll": roll,
            "status_text": status_text,
            "status_color": status_color,
            "warning_msg": warning_msg,
            "left_eye_pts": left_eye,
            "right_eye_pts": right_eye,
        }
