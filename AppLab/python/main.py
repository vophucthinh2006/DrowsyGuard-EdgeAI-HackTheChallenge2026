"""
Driver Monitoring - calls the provided .tflite models directly (no .task
file, no Edge Impulse Runner / .eim needed - see README.md for why).

Pipeline per frame:
  1. face_detector.tflite            -> face bounding box (BlazeFace)
  2. face_landmarks_detector.tflite  -> 478 3D landmarks inside that bbox
  3. custom_mobilenetv3_v1_float16.tflite -> mask / sunglasses check
  4. If the face is NOT obstructed   -> drowsiness/yawn/head-pose check

DISPLAY:
  This app tries a native OpenCV window first (useful when running the
  script directly on a PC/laptop for development). If no display is
  available - which is the normal case when this runs as an Arduino App
  Lab Python app inside its headless container - it automatically falls
  back to a built-in MJPEG web server (see webui.py), viewable from any
  browser at http://<board-ip>:7000/.

PERFORMANCE TUNING KNOBS (lower these on a weaker board like UNO Q):
  NUM_THREADS, DETECT_EVERY, CAM_WIDTH, CAM_HEIGHT

Run: python main.py   |   press "q" to quit (native window mode only).
"""
import os
import time
from collections import deque

import cv2
import numpy as np

from blaze_face import BlazeFaceDetector
from face_landmark import FaceLandmarkDetector
from landmark_regions import EYES_AND_MOUTH, MOUTH_OUTLINE
from drowsiness import DrowsinessMonitor
from obstruction_detector import ObstructionDetector
from webui import WebStreamer

# ---- PERFORMANCE TUNING ----
NUM_THREADS = 4        # set to the actual number of CPU cores on your board
DETECT_EVERY = 15      # 1 = always run real detection; higher = smoother on weak boards
CAM_WIDTH = 1280
CAM_HEIGHT = 720
WEB_PORT = 7000         # matches the port used by Arduino's own WebUI examples
# ------------------------------

# ---- DISPLAY STYLE ----
FONT = cv2.FONT_HERSHEY_SIMPLEX
FONT_SCALE_INFO = 0.7
FONT_SCALE_WARNING = 1.3
FONT_THICKNESS_INFO = 2
FONT_THICKNESS_WARNING = 3
POINT_RADIUS = 2
# ------------------------------


def _native_window_available():
    """Best-effort check for whether cv2.imshow can actually open a window.
    Inside the App Lab headless container this will be False, so the app
    automatically switches to the MJPEG web view instead."""
    if os.name != "nt" and not os.environ.get("DISPLAY"):
        return False
    try:
        cv2.namedWindow("_probe", cv2.WINDOW_NORMAL)
        cv2.destroyWindow("_probe")
        return True
    except cv2.error:
        return False


def main():
    detector = BlazeFaceDetector(num_threads=NUM_THREADS)
    landmarker = FaceLandmarkDetector(num_threads=NUM_THREADS)
    monitor = DrowsinessMonitor()
    obs_detector = ObstructionDetector(num_threads=NUM_THREADS)

    use_native_window = _native_window_available()
    streamer = None
    if not use_native_window:
        streamer = WebStreamer(port=WEB_PORT)
        streamer.start()
        print("[INFO] No local display detected - streaming video over the web instead.")

    cap = cv2.VideoCapture(0)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, CAM_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, CAM_HEIGHT)
    if not cap.isOpened():
        raise RuntimeError("Could not open the webcam. Check the camera index / permissions.")

    lat_detect = deque(maxlen=30)
    lat_landmark = deque(maxlen=30)
    lat_total = deque(maxlen=30)

    prev_bbox = None
    frame_idx = 0

    print("[INFO] Camera opened. " + ("Press 'q' to quit..." if use_native_window else "Streaming started."))

    while cap.isOpened():
        t_start = time.perf_counter()
        ok, frame_bgr = cap.read()
        if not ok:
            break

        frame_bgr = cv2.flip(frame_bgr, 1)  # mirror effect
        frame_rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
        h_frame, w_frame = frame_bgr.shape[:2]

        need_detect = prev_bbox is None or frame_idx % DETECT_EVERY == 0

        # --- Step 1: call face_detector.tflite (only when needed) ---
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
            # --- Step 2: call face_landmarks_detector.tflite ---
            t2 = time.perf_counter()
            landmarks, face_score = landmarker.predict(frame_rgb, bbox)
            t3 = time.perf_counter()
            lat_landmark.append((t3 - t2) * 1000)

            if landmarks is not None and face_score > 0.5:
                prev_bbox = FaceLandmarkDetector.landmarks_to_bbox(landmarks)
                x1, y1, x2, y2 = map(int, bbox)

                # Crop the face region (10% padding) to check for obstructions
                pad_w = int((x2 - x1) * 0.1)
                pad_h = int((y2 - y1) * 0.1)
                crop_x1, crop_x2 = max(0, x1 - pad_w), min(w_frame, x2 + pad_w)
                crop_y1, crop_y2 = max(0, y1 - pad_h), min(h_frame, y2 + pad_h)

                face_crop = frame_bgr[crop_y1:crop_y2, crop_x1:crop_x2]

                # --- Step 3: check for obstructions (gate condition) ---
                obs_res = obs_detector.predict(face_crop)

                # Only run drowsiness checks if the face is unobstructed
                if not obs_res["is_obstructed"]:
                    info = monitor.update(landmarks, frame_bgr.shape)
            else:
                prev_bbox = None
                landmarks = None
        else:
            prev_bbox = None

        # ==== DRAW ON THE FRAME ====
        if bbox is not None:
            x1, y1, x2, y2 = map(int, bbox)

            # --- CASE 1: FACE OBSTRUCTED (MASK / SUNGLASSES) ---
            if obs_res and obs_res["is_obstructed"]:
                cv2.rectangle(frame_bgr, (x1, y1), (x2, y2), (0, 0, 255), 2)

                if obs_res["has_mask"] and obs_res["has_sunglasses"]:
                    msg = "WARNING: Please remove mask AND sunglasses!"
                elif obs_res["has_mask"]:
                    msg = "WARNING: Please remove your mask!"
                else:
                    msg = "WARNING: Please remove your sunglasses!"

                cv2.putText(frame_bgr, msg, (10, 50),
                            FONT, FONT_SCALE_WARNING, (0, 0, 255), FONT_THICKNESS_WARNING, cv2.LINE_AA)
                cv2.putText(frame_bgr, f"Mask: {obs_res['prob_mask']:.2f} | Glasses: {obs_res['prob_sunglasses']:.2f}",
                            (10, 90), FONT, FONT_SCALE_INFO, (0, 0, 255), FONT_THICKNESS_INFO, cv2.LINE_AA)

            # --- CASE 2: FACE CLEAR -> DRAW MESH + DROWSINESS STATUS ---
            elif landmarks is not None and info is not None:
                for idx in EYES_AND_MOUTH:
                    x, y, _ = landmarks[idx]
                    cv2.circle(frame_bgr, (int(x), int(y)), POINT_RADIUS, (0, 255, 0), -1)

                cv2.polylines(frame_bgr, [np.array(info["left_eye_pts"], dtype=np.int32)], True, (0, 255, 0), 2)
                cv2.polylines(frame_bgr, [np.array(info["right_eye_pts"], dtype=np.int32)], True, (0, 255, 0), 2)
                mouth_pts = np.array([landmarks[i][:2] for i in MOUTH_OUTLINE], dtype=np.int32)
                cv2.polylines(frame_bgr, [mouth_pts], True, (0, 255, 255), 2)

                cv2.rectangle(frame_bgr, (x1, y1), (x2, y2), (255, 0, 0), 2)

                cv2.putText(frame_bgr, f"Status: {info['status_text']}", (10, 40),
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

        if use_native_window:
            cv2.imshow("Driver Monitoring - calling .tflite models directly", frame_bgr)
            if cv2.waitKey(1) & 0xFF == ord("q"):
                break
        else:
            streamer.update_frame(frame_bgr)

        frame_idx += 1

    cap.release()
    if use_native_window:
        cv2.destroyAllWindows()
    else:
        streamer.stop()


if __name__ == "__main__":
    main()
