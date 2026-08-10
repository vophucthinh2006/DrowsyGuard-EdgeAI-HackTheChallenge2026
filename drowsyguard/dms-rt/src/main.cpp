#include <Arduino.h>
#include <stdint.h>
// #include "drowsyguard_can.h" // Sẽ sinh ra từ icd.yaml bằng Python script

// Trạng thái cục bộ (DEV-027)
typedef enum {
    DMS_STATE_INIT = 0,
    DMS_STATE_RUN = 1,
    DMS_STATE_FAULT = 2
} dms_state_t;

static dms_state_t s_current_state = DMS_STATE_INIT;
static uint8_t s_alert_level = 0; 
static uint32_t s_last_rpc_time = 0;

void setup() {
    Serial.begin(115200); // Giao tiếp RPC nội bộ với Linux
    
    // Khởi tạo FDCAN hoặc SPI CAN (MCP2515) ở đây (Plan B - OI-04-01)
    // CAN_Init(500000); 

    s_current_state = DMS_STATE_RUN;
}

void loop() {
    uint32_t current_time = millis();

    switch (s_current_state) {
        case DMS_STATE_RUN:
            // 1. Đọc tín hiệu từ nhân Linux (Python gửi xuống)
            if (Serial.available() > 0) {
                s_alert_level = Serial.read(); // Đọc 1 byte
                s_last_rpc_time = current_time;
            }

            // 2. Gửi frame 0x100 DMS_STATUS định kỳ đúng 100ms (CAN-010, CAN-011)
            static uint32_t s_last_can_tx = 0;
            if (current_time - s_last_can_tx >= 100) {
                s_last_can_tx = current_time;
                
                // uint8_t can_payload[8] = {0};
                // pack_dms_status_frame(can_payload, s_alert_level, ...);
                // CAN_Transmit(0x100, can_payload, 8);
            }

            // 3. Giám sát Timeout (Nếu Python treo)
            if (current_time - s_last_rpc_time > 300) {
                // log ERROR (CAN-065)
                s_current_state = DMS_STATE_FAULT;
            }
            break;

        case DMS_STATE_FAULT:
            // Xử lý mất kết nối, vẫn giữ nhịp gửi CAN_STATUS nhưng set cờ Fault
            break;

        default:
            s_current_state = DMS_STATE_FAULT; // Bắt lỗi state rác
            break;
    }
}