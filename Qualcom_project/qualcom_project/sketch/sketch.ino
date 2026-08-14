// SPDX-FileCopyrightText: Copyright (C) Arduino s.r.l. and/or its affiliated companies
//
// SPDX-License-Identifier: MPL-2.0

/*
   DỰ ÁN KHỬ THỬ NGHIỆM GIAO TIẾP ĐA KÊNH MCU <-> MPU (TEST BRIDGING & MULTI LOGGING)
   -------------------------------------------------------------------------------
   Tệp Sketch C++ chạy phía MCU Arduino:
   1. Khởi tạo đồng thời 3 Cổng Log:
      - Monitor.begin(115200) : Internal App Lab Serial Monitor
      - Serial.begin(115200)  : USB Serial
      - Serial1.begin(115200) : Hardware Serial1 (UART Pins)
   2. Tiếp nhận RPC từ MPU Python qua function `send_test_packet`.
   3. Xuất Log ĐỒNG THỜI ra cả 3 nơi thông qua hàm `logAll(...)`.
   4. Phản hồi tín hiệu ngược lại MPU qua `Bridge.notify("on_mcu_reply", ...)`.
   5. Đọc chuỗi ký tự từ Serial / Serial1 và gửi lên MPU qua `Bridge.notify("on_external_cmd", ...)`.
*/

#include "Arduino_RouterBridge.h"

#define BAUD_RATE 115200

String rxBufferSerial = "";
String rxBufferSerial1 = "";

// ======================================================
// HÀM TRỢ LÝ: XUẤT LOG ĐỒNG THỜI RA TẤT CẢ CÁC CỔNG (MONITOR, SERIAL, SERIAL1)
// ======================================================
void logAll(const String& msg) {
    Monitor.println(msg);    // 1. Log bên trong App Lab (Internal Monitor)
    Serial.println(msg);     // 2. Log ra USB Serial
    Serial1.println(msg);    // 3. Log ra Hardware Serial1 (UART Pins)
}

// ======================================================
// HÀM RPC: TIẾP NHẬN GÓI DỮ LIỆU TỪ MPU PYTHON
// ======================================================
void send_test_packet(int seq, float ear, float mar) {
    // 1. Tạo chuỗi Log định dạng rõ ràng
    String logHeader = "⚡ [MCU Rx <- MPU] Nhận gói test #" + String(seq) +
                       " | EAR: " + String(ear, 2) +
                       " | MAR: " + String(mar, 2);

    // 2. Xuất Log ĐỒNG THỜI ra 3 cổng: Monitor, Serial, Serial1
    logAll(logHeader);

    // 3. Đánh giá logic thử nghiệm đơn giản
    String statusStr = "NORMAL";
    if (ear < 0.15f) {
        statusStr = "ALERT: EYE_CLOSED";
    } else if (mar > 0.60f) {
        statusStr = "WARN: YAWNING";
    }

    String logStatus = "   └─ STATUS: " + statusStr;
    logAll(logStatus);

    // 4. Phản hồi tín hiệu ngược lại MPU Python qua Bridge.notify
    String replyMsg = "ACK_FRAME_" + String(seq) + "_STATUS_" + statusStr;
    Bridge.notify("on_mcu_reply", replyMsg);
}

void setup() {
    // Khởi tạo đồng thời cả 3 cổng log với baudrate 115200
    Monitor.begin(BAUD_RATE);   // Internal App Lab Monitor
    Serial.begin(BAUD_RATE);    // USB Serial
    Serial1.begin(BAUD_RATE);   // Hardware Serial1

    logAll("==================================================");
    logAll("  TEST BRIDGING & TRIPLE LOGGING (MONITOR, SERIAL, SERIAL1)");
    logAll("  • Internal Monitor : ACTIVE (115200 baud)");
    logAll("  • USB Serial       : ACTIVE (115200 baud)");
    logAll("  • Hardware Serial1 : ACTIVE (115200 baud)");
    logAll("==================================================");

    Bridge.begin();
    
    // Đăng ký hàm RPC để Python phía MPU có thể gọi xuống
    Bridge.provide("send_test_packet", send_test_packet);
}

void loop() {
    // 1. Đọc lệnh từ cổng USB Serial
    while (Serial.available() > 0) {
        char c = Serial.read();
        if (c == '\n') {
            rxBufferSerial.trim();
            if (rxBufferSerial.length() > 0) {
                logAll("📥 [MCU Serial Rx] Nhận lệnh từ USB Serial: " + rxBufferSerial);
                Bridge.notify("on_external_cmd", "Serial: " + rxBufferSerial);
            }
            rxBufferSerial = "";
        } else {
            rxBufferSerial += c;
        }
    }

    // 2. Đọc lệnh từ cổng Hardware Serial1
    while (Serial1.available() > 0) {
        char c = Serial1.read();
        if (c == '\n') {
            rxBufferSerial1.trim();
            if (rxBufferSerial1.length() > 0) {
                logAll("📥 [MCU Serial1 Rx] Nhận lệnh từ Serial1: " + rxBufferSerial1);
                Bridge.notify("on_external_cmd", "Serial1: " + rxBufferSerial1);
            }
            rxBufferSerial1 = "";
        } else {
            rxBufferSerial1 += c;
        }
    }
}
