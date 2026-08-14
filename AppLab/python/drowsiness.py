"""
Drowsiness / yawn / distraction detection logic, ported from the two sample
driver-monitoring files (which used mediapipe.tasks +
facial_transformation_matrixes). Since this project does not use the Tasks
API / .task file, head-pose is estimated instead via cv2.solvePnP (a common
technique that only needs 6 landmarks + an approximate 3D face model,
avoiding the need for MediaPipe's full geometry pipeline).

Usage: create one DrowsinessMonitor(), then each frame call
monitor.update(landmarks, frame_shape) with landmarks being the (478,3)
array returned by FaceLandmarkDetector.predict().
"""
import numpy as np
import cv2

from landmark_regions import EAR_LEFT_EYE, EAR_RIGHT_EYE, MOUTH_MAR, HEAD_POSE_IDX

# ==== ALERT THRESHOLDS (tune these to match real-world testing) ====
EAR_THRESHOLD = 0.22
EAR_CONSECUTIVE_FRAMES = 15

MAR_THRESHOLD = 0.55
MAR_CONSECUTIVE_FRAMES = 15

YAW_THRESHOLD = 25      # degrees
PITCH_THRESHOLD = 20    # degrees
POSE_CONSECUTIVE_FRAMES = 20

# Approximate 3D model of the 6 reference points (arbitrary units - only the
# relative proportions between points matter). Order must match
# HEAD_POSE_IDX in landmark_regions.py: [nose, chin, left eye corner,
# right eye corner, left mouth corner, right mouth corner]
_MODEL_3D_POINTS = np.array([
    (0.0, 0.0, 0.0),        # nose tip (origin)
    (0.0, -63.6, -12.5),    # chin
    (-43.3, 32.7, -26.0),   # left eye corner
    (43.3, 32.7, -26.0),    # right eye corner
    (-28.9, -28.9, -24.1),  # left mouth corner
    (28.9, -28.9, -24.1),   # right mouth corner
], dtype=np.float64)


def _euclidean(p1, p2):
    return float(np.hypot(p1[0] - p2[0], p1[1] - p2[1]))


def eye_aspect_ratio(coords):
    """coords: 6 points (x,y) in the same order as EAR_LEFT_EYE/EAR_RIGHT_EYE."""
    a = _euclidean(coords[1], coords[5])
    b = _euclidean(coords[2], coords[4])
    c = _euclidean(coords[0], coords[3])
    return (a + b) / (2.0 * c) if c > 0 else 0.0


def mouth_aspect_ratio(coords):
    """coords: 4 points (x,y) in the same order as MOUTH_MAR = [left, right, top, bottom]."""
    vertical = _euclidean(coords[2], coords[3])
    horizontal = _euclidean(coords[0], coords[1])
    return vertical / horizontal if horizontal > 0 else 0.0


def estimate_head_pose(landmarks_px, frame_shape):
    """Estimates (pitch, yaw, roll) via solvePnP from 6 2D landmarks.
    This is an approximation (a generic, normalized 3D model rather than a
    model fit to each individual face) - good enough to catch clear head
    turns/tilts."""
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

    # guard against the 180-degree flip ambiguity
    if pitch > 90:
        pitch = 180.0 - pitch
    elif pitch < -90:
        pitch = -180.0 - pitch

    return float(pitch), float(yaw), float(roll)


class DrowsinessMonitor:
    """Keeps per-frame counters and returns info ready to be drawn on screen."""

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

        status_text = "ALERT"
        status_color = (0, 255, 0)
        warning_msg = ""

        if avg_ear < EAR_THRESHOLD:
            self.ear_counter += 1
            if self.ear_counter >= EAR_CONSECUTIVE_FRAMES:
                warning_msg = "WARNING: DROWSINESS DETECTED!"
        else:
            self.ear_counter = 0

        if mar > MAR_THRESHOLD:
            self.mar_counter += 1
            if self.mar_counter >= MAR_CONSECUTIVE_FRAMES:
                warning_msg = "WARNING: YAWNING DETECTED!"
        else:
            self.mar_counter = 0

        if abs(yaw) > YAW_THRESHOLD or abs(pitch) > PITCH_THRESHOLD:
            self.pose_counter += 1
            if self.pose_counter >= POSE_CONSECUTIVE_FRAMES:
                warning_msg = "WARNING: DRIVER DISTRACTED!"
        else:
            self.pose_counter = 0

        if warning_msg:
            status_text = "DANGER"
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
