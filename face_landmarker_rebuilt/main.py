"""
Goi truc tiep 2 model .tflite co san (khong dung file .task).

Ban nay:
  - Khung hinh to hon, chu to/ro hon (de nhin khi demo/trinh chieu).
  - Tich hop cong chan Kiem tra vat che (Obstruction Check) truoc khi do
    canh bao buon ngu (EAR) / ngap (MAR) / mat tap trung (head-pose).

THAM SO TINH CHINH HIEU NANG (giam neu chay tren board yeu nhu UNO Q):
  NUM_THREADS, DETECT_EVERY, CAM_WIDTH, CAM_HEIGHT

Chay: python main.py   |   nhan "q" de thoat.
"""
import time
from collections import deque

import cv2
import numpy as np

from blaze_face import BlazeFaceDetector
from face_landmark import FaceLandmarkDetector
from landmark_regions import EYES_AND_MOUTH, MOUTH_OUTLINE
from drowsiness import DrowsinessMonitor
from obstruction_detector import ObstructionDetector

# ---- THAM SO TINH CHINH HIEU NANG ----
NUM_THREADS = 4        # doi thanh so nhan CPU thuc te cua board ban dung
DETECT_EVERY = 15      # 1 = luon detect that; tang len de chay muot hon tren board yeu
CAM_WIDTH = 1280        # to hon de nhin ro luoi diem / demo
CAM_HEIGHT = 720
# ----------------------------------------

# ---- THAM SO HIEN THI ----
FONT = cv2.FONT_HERSHEY_SIMPLEX
FONT_SCALE_INFO = 0.7
FONT_SCALE_WARNING = 1.3
FONT_THICKNESS_INFO = 2
FONT_THICKNESS_WARNING = 3
POINT_RADIUS = 2
# ----------------------------------------


def main():
    detector = BlazeFaceDetector(num_threads=NUM_THREADS)
    landmarker = FaceLandmarkDetector(num_threads=NUM_THREADS)
    monitor = DrowsinessMonitor()
    obs_detector = ObstructionDetector(num_threads=NUM_THREADS)

    cap = cv2.VideoCapture(0)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, CAM_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, CAM_HEIGHT)
    if not cap.isOpened():
        raise RuntimeError("Khong mo duoc webcam. Kiem tra lai camera index / quyen truy cap.")

    lat_detect = deque(maxlen=30)
    lat_landmark = deque(maxlen=30)
    lat_total = deque(maxlen=30)

    prev_bbox = None
    frame_idx = 0

    print("[INFO] Dang mo camera. Nhan 'q' de thoat...")

    while cap.isOpened():
        t_start = time.perf_counter()
        ok, frame_bgr = cap.read()
        if not ok:
            break

        frame_bgr = cv2.flip(frame_bgr, 1)  # hieu ung guong
        frame_rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
        h_frame, w_frame = frame_bgr.shape[:2]

        need_detect = prev_bbox is None or frame_idx % DETECT_EVERY == 0

        # --- Buoc 1: goi face_detector.tflite (chi khi can) ---
        bbox = prev_bbox
        if need_detect:
            t0 = time.perf_counter()
            faces = detector.detect(frame_rgb)
            t1 = time.perf_counter()
            lat_detect.append((t1 - t0) * 1000)
            bbox = None
            if faces:
                faces.sort(key=lambda f: f["score"], reverse=True)
                bbox = faces[0]["bbox"]

        landmarks, face_score = None, 0.0
        info = None
        obs_res = None

        if bbox is not None:
            # --- Buoc 2: goi face_landmarks_detector.tflite ---
            t2 = time.perf_counter()
            landmarks, face_score = landmarker.predict(frame_rgb, bbox)
            t3 = time.perf_counter()
            lat_landmark.append((t3 - t2) * 1000)

            if landmarks is not None and face_score > 0.5:
                prev_bbox = FaceLandmarkDetector.landmarks_to_bbox(landmarks)
                x1, y1, x2, y2 = map(int, bbox)

                # Crop vung khuon mat (padding 10%) de kiem tra vat che
                pad_w = int((x2 - x1) * 0.1)
                pad_h = int((y2 - y1) * 0.1)
                crop_x1, crop_x2 = max(0, x1 - pad_w), min(w_frame, x2 + pad_w)
                crop_y1, crop_y2 = max(0, y1 - pad_h), min(h_frame, y2 + pad_h)

                face_crop = frame_bgr[crop_y1:crop_y2, crop_x1:crop_x2]

                # --- Buoc 3: Kiem tra vat che (Cong chan / Logic Gate) ---
                obs_res = obs_detector.predict(face_crop)

                # Chi tinh do buon ngu NẾU MAT SACH (Khong deo khau trang & kinh ram)
                if not obs_res["is_obstructed"]:
                    info = monitor.update(landmarks, frame_bgr.shape)
            else:
                prev_bbox = None
                landmarks = None
        else:
            prev_bbox = None

        # ==== VE LEN KHUNG HINH ====
        if bbox is not None:
            x1, y1, x2, y2 = map(int, bbox)

            # --- TRUONG HOP 1: BI CHE (KHAU TRANG / KINH RAM) ---
            if obs_res and obs_res["is_obstructed"]:
                cv2.rectangle(frame_bgr, (x1, y1), (x2, y2), (0, 0, 255), 2)

                if obs_res["has_mask"] and obs_res["has_sunglasses"]:
                    msg = "CANH BAO: Thao khau trang VA kinh ram!"
                elif obs_res["has_mask"]:
                    msg = "CANH BAO: Vui long thao khau trang!"
                else:
                    msg = "CANH BAO: Vui long thao kinh ram!"

                cv2.putText(frame_bgr, msg, (10, 50),
                            FONT, FONT_SCALE_WARNING, (0, 0, 255), FONT_THICKNESS_WARNING, cv2.LINE_AA)
                cv2.putText(frame_bgr, f"Mask: {obs_res['prob_mask']:.2f} | Glasses: {obs_res['prob_sunglasses']:.2f}",
                            (10, 90), FONT, FONT_SCALE_INFO, (0, 0, 255), FONT_THICKNESS_INFO, cv2.LINE_AA)

            # --- TRUONG HOP 2: MAT SACH -> VE LUOI DIEM VA CANH BAO BUON NGU ---
            elif landmarks is not None and info is not None:
                for idx in EYES_AND_MOUTH:
                    x, y, _ = landmarks[idx]
                    cv2.circle(frame_bgr, (int(x), int(y)), POINT_RADIUS, (0, 255, 0), -1)

                cv2.polylines(frame_bgr, [np.array(info["left_eye_pts"], dtype=np.int32)], True, (0, 255, 0), 2)
                cv2.polylines(frame_bgr, [np.array(info["right_eye_pts"], dtype=np.int32)], True, (0, 255, 0), 2)
                mouth_pts = np.array([landmarks[i][:2] for i in MOUTH_OUTLINE], dtype=np.int32)
                cv2.polylines(frame_bgr, [mouth_pts], True, (0, 255, 255), 2)

                cv2.rectangle(frame_bgr, (x1, y1), (x2, y2), (255, 0, 0), 2)

                # trang thai + canh bao (chu to, de doc)
                cv2.putText(frame_bgr, f"Trang thai: {info['status_text']}", (10, 40),
                            FONT, FONT_SCALE_INFO + 0.1, info["status_color"], FONT_THICKNESS_INFO, cv2.LINE_AA)
                if info["warning_msg"]:
                    cv2.putText(frame_bgr, info["warning_msg"], (10, 90),
                                FONT, FONT_SCALE_WARNING, (0, 0, 255), FONT_THICKNESS_WARNING, cv2.LINE_AA)

                cv2.putText(frame_bgr, f"EAR: {info['ear']:.2f}", (10, 130),
                            FONT, FONT_SCALE_INFO, (255, 255, 0), FONT_THICKNESS_INFO, cv2.LINE_AA)
                cv2.putText(frame_bgr, f"MAR: {info['mar']:.2f}", (10, 165),
                            FONT, FONT_SCALE_INFO, (255, 255, 0), FONT_THICKNESS_INFO, cv2.LINE_AA)
                cv2.putText(frame_bgr, f"Pitch:{info['pitch']:.0f}  Yaw:{info['yaw']:.0f}", (10, 200),
                            FONT, FONT_SCALE_INFO, (255, 255, 0), FONT_THICKNESS_INFO, cv2.LINE_AA)

        lat_total.append((time.perf_counter() - t_start) * 1000)

        def avg(dq):
            return sum(dq) / len(dq) if dq else 0.0

        fps = 1000.0 / avg(lat_total) if avg(lat_total) > 0 else 0.0
        perf_lines = [
            f"FPS: {fps:5.1f}",
            f"detect: {avg(lat_detect):5.1f} ms | landmark: {avg(lat_landmark):5.1f} ms",
        ]
        for i, line in enumerate(perf_lines):
            cv2.putText(frame_bgr, line, (10, h_frame - 45 + i * 30),
                        FONT, FONT_SCALE_INFO, (0, 255, 255), FONT_THICKNESS_INFO, cv2.LINE_AA)

        cv2.imshow("Driver Monitoring - goi truc tiep model .tflite", frame_bgr)
        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

        frame_idx += 1

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()