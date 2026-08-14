# SPDX-FileCopyrightText: Copyright (C) Arduino s.r.l. and/or its affiliated companies
# SPDX-License-Identifier: MPL-2.0
"""
DỰ ÁN THỬ NGHIỆM GIAO TIẾP ĐA KÊNH MCU <-> MPU + WEB UI
-------------------------------------------------------------------------------
1. Xuất log hiệu năng ra Terminal.
2. Đóng gói truyền dữ liệu RPC xuống MCU.
3. Tiếp nhận thông điệp phản hồi từ MCU.
4. TÍCH HỢP WEB UI: Cung cấp API và phát dữ liệu thời gian thực qua WebSocket.
"""

import time
from datetime import datetime, UTC
from arduino.app_utils import App, Bridge, Logger
from arduino.app_bricks.web_ui import WebUI  # <-- 1. Import thư viện Web UI

logger = Logger("MPU_Test_Bridge_Web")

# Khởi tạo Web UI server
web_ui = WebUI()

# Thêm một REST API endpoint cơ bản theo example
web_ui.expose_api("GET", "/hello", lambda: {"message": "Hello, world! MCU-MPU system is running."})

# Biến toàn cục theo dõi thời gian và số thứ tự khung hình
last_time = None

seq_counter = 0

def on_mcu_reply(reply_msg: str):
    """ Callback tiếp nhận thông điệp đẩy từ MCU lên MPU """
    print(f"📩 [MPU Rx <- MCU] Phản hồi từ MCU: {reply_msg}")
    
    # Bắn thông điệp từ MCU thẳng lên Web UI để hiển thị log
    web_ui.send_message("mcu_log", {"source": "MCU", "message": reply_msg})

def on_external_cmd(raw_cmd: str):
    """ Callback tiếp nhận lệnh do MCU đọc từ cổng UART ngoại vi """
    print(f"📡 [MPU Rx <- External UART] Lệnh nhận từ thiết bị ngoài: {raw_cmd}")
    
    # Bắn lệnh ngoại vi lên Web UI
    web_ui.send_message("uart_log", {"source": "UART", "command": raw_cmd})

# Đăng ký các hàm giao tiếp để MCU có thể gọi lên MPU
Bridge.provide("on_mcu_reply", on_mcu_reply)
Bridge.provide("on_external_cmd", on_external_cmd)

def user_loop():
    """ Vòng lặp thử nghiệm chính trên MPU (Chu kỳ 1.0 giây) """
    global last_time, seq_counter
    seq_counter += 1
    current_time = time.perf_counter()

    # 1. TÍNH TOÁN HIỆU NĂNG
    if last_time is not None:
        cycle_ms = (current_time - last_time) * 1000.0
        fps = 1.0 / (current_time - last_time) if (current_time - last_time) > 0 else 0.0
    else:
        cycle_ms = 0.0
        fps = 0.0
    last_time = current_time
    
    # 2. XUẤT LOG COMMAND LINE
    print("-----------------------------------------------------------------")
    print(f"🖥️ [Terminal] Frame #{seq_counter} | Cycle: {cycle_ms:.2f} ms | FPS: {fps:.2f}")

    # Giả lập dữ liệu
    test_ear = 0.12 if seq_counter % 2 == 1 else 0.25
    test_mar = 0.65 if seq_counter % 3 == 0 else 0.18

    # 3. GỬI DỮ LIỆU XUỐNG MCU (RPC)
    print(f"📤 [MPU Tx -> MCU] Gửi gói test #{seq_counter}")
    try:
        Bridge.call("send_test_packet", seq_counter, float(test_ear), float(test_mar))
    except Exception as e:
        logger.error(f"❌ Lỗi khi gửi dữ liệu xuống MCU: {e}")

    # 4. GỬI DỮ LIỆU LÊN WEB UI (WEBSOCKET)
    # Đóng gói dữ liệu thành dictionary (JSON format)
    telemetry_data = {
        "frame": seq_counter,
        "cycle_ms": round(cycle_ms, 2),
        "fps": round(fps, 2),
        "ear": test_ear,
        "mar": test_mar
    }

    # Phát sự kiện "telemetry_update" đến tất cả các client web đang kết nối
    web_ui.send_message("telemetry_update", telemetry_data)

    # Tạm dừng 1 giây
    time.sleep(1.0)

# Khởi động Web UI Server TRƯỚC khi chạy vòng lặp chính của App
print("🌐 Đang khởi động Web UI Server...")
web_ui.start()

# Chạy vòng lặp ứng dụng
App.run(user_loop=user_loop)