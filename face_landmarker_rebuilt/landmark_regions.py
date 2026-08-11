"""
Chi so (index) cua cac diem moc thuoc vung MAT va MIENG, theo dung thu tu 478
diem ma face_landmarks_detector.tflite tra ve (chuan MediaPipe FaceMesh).

Model van tinh toan DU 478 diem trong 1 lan chay (khong the bat model chi
tinh rieng vung mat/mieng) — file nay chi dung de LOC lai phan can hien thi/
xu ly tiep theo, giup giam chi phi ve (drawing) va giam du lieu can xu ly o
buoc sau (vd tinh EAR de phat hien nham mat, MAR de phat hien mo mieng...).
"""

LEFT_EYE = [
    33, 7, 163, 144, 145, 153, 154, 155, 133,
    246, 161, 160, 159, 158, 157, 173,
]

RIGHT_EYE = [
    263, 249, 390, 373, 374, 380, 381, 382, 362,
    466, 388, 387, 386, 385, 384, 398,
]

# Vien ngoai + vien trong moi
MOUTH = [
    61, 146, 91, 181, 84, 17, 314, 405, 321, 375, 291,
    308, 324, 318, 402, 317, 14, 87, 178, 88, 95,
    78, 191, 80, 81, 82, 13, 312, 311, 310, 415,
]

EYES_AND_MOUTH = sorted(set(LEFT_EYE + RIGHT_EYE + MOUTH))

# ---- Chi so rieng cho tinh EAR / MAR (buon ngu, ngap) - lay tu file mau ----
# 6 diem moi mat, dung cong thuc EAR chuan (Soukupova & Cech)
EAR_LEFT_EYE = [362, 385, 387, 263, 373, 380]
EAR_RIGHT_EYE = [33, 160, 158, 133, 153, 144]

# 4 diem mieng de tinh MAR: [trai, phai, tren, duoi]
MOUTH_MAR = [78, 308, 13, 14]

# Vien moi trong, dung de VE duong vien mieng
MOUTH_OUTLINE = [
    78, 191, 80, 81, 82, 13, 312, 311, 310, 415,
    308, 324, 318, 402, 317, 14, 87, 178, 88, 95,
]

# 6 diem chuan de uoc luong huong dau (head pose) bang cv2.solvePnP:
# mui, cam, khoe mat trai, khoe mat phai, khoe mieng trai, khoe mieng phai
HEAD_POSE_IDX = [1, 152, 33, 263, 61, 291]
