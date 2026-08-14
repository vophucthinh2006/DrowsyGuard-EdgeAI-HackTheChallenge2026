"""
Indices for the landmarks that belong to the EYE and MOUTH regions, in the
exact order that face_landmarks_detector.tflite returns its 478 points
(standard MediaPipe FaceMesh indexing).

The model always computes all 478 points in a single inference pass (it
cannot be told to compute only the eye/mouth region) - this file only
FILTERS which points get displayed/processed further, which reduces
drawing cost and the amount of data used downstream (e.g. computing EAR for
eye-closure detection, MAR for mouth-open/yawn detection...).
"""

LEFT_EYE = [
    33, 7, 163, 144, 145, 153, 154, 155, 133,
    246, 161, 160, 159, 158, 157, 173,
]

RIGHT_EYE = [
    263, 249, 390, 373, 374, 380, 381, 382, 362,
    466, 388, 387, 386, 385, 384, 398,
]

# Outer + inner lip contour
MOUTH = [
    61, 146, 91, 181, 84, 17, 314, 405, 321, 375, 291,
    308, 324, 318, 402, 317, 14, 87, 178, 88, 95,
    78, 191, 80, 81, 82, 13, 312, 311, 310, 415,
]

EYES_AND_MOUTH = sorted(set(LEFT_EYE + RIGHT_EYE + MOUTH))

# ---- Indices specifically for EAR / MAR (drowsiness, yawning) ----
# 6 points per eye, using the standard EAR formula (Soukupova & Cech)
EAR_LEFT_EYE = [362, 385, 387, 263, 373, 380]
EAR_RIGHT_EYE = [33, 160, 158, 133, 153, 144]

# 4 mouth points used for MAR: [left, right, top, bottom]
MOUTH_MAR = [78, 308, 13, 14]

# Inner lip contour, used to DRAW the mouth outline
MOUTH_OUTLINE = [
    78, 191, 80, 81, 82, 13, 312, 311, 310, 415,
    308, 324, 318, 402, 317, 14, 87, 178, 88, 95,
]

# 6 standard points used to estimate head pose via cv2.solvePnP:
# nose tip, chin, left eye corner, right eye corner, left mouth corner,
# right mouth corner
HEAD_POSE_IDX = [1, 152, 33, 263, 61, 291]
