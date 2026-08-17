// DrowsyGuard DMS-RT -- STM32U585 side of the Arduino UNO Q.
//
// Deploy-ready. Started as a scratch/test copy of
// ../../QUALCOMM_AI/DrowsyGuard-EdgeAI-HackTheChallenge2026/dms-ap/app/sketch/sketch.ino
// with the one piece that file explicitly left unimplemented now filled
// in and CONFIRMED WORKING ON REAL HARDWARE (2026-08-15, both directions,
// against a real vcs-mcxn947 board over a real CAN bus): FDCAN1.
//
// FDCAN1 uses Arduino's own first-party `CAN` library (`CAN.h`, shipped
// inside the `arduino:zephyr` core package itself, with `CANRead`/
// `CANWrite`/`CANEvent` examples) rather than raw Zephyr driver calls --
// see ../README.md "How FDCAN1 actually got working" for the two wrong
// turns that preceded this (raw Zephyr `zephyr/drivers/can.h` calls
// compiled but failed at runtime with "device not ready": the board's own
// shipped devicetree marks `&fdcan1` `zephyr,deferred-init`, needing an
// explicit `device_init()` this library calls internally via
// `CAN.begin()`, along with setting the bitrate to 500 kbit/s -- the
// board default is 125 kbit/s).
//
// External hardware this sketch assumes is already wired (see
// ../README.md "Hardware setup that is CONFIRMED to work" for exactly
// what was used and why): a CAN transceiver on D4(TX)/D5(RX) -- the UNO Q
// has no onboard one, unlike vcs-mcxn947's onboard TJA1057 -- bridging to
// vcs-mcxn947's CAN_H/CAN_L (J10), 120 ohm termination at each physical
// bus end (CAN-002).
//
// SPDX-License-Identifier: BSD-3-Clause

#include "Arduino_RouterBridge.h"

#include <CAN.h>

// ---- ICD byte layout, mirrors ../python/drowsyguard/link/icd.py and
// vcs-mcxn947/src/icd/icd.h byte-for-byte (specs/04 §3, shared/icd/icd.yaml).
#define CANID_EMERGENCY_STOP (0x080U)
#define CANID_DMS_STATUS     (0x100U)
#define CANID_DMS_METRICS    (0x101U)
#define CANID_VCS_STATUS     (0x200U)
#define CANID_VCS_EVENT      (0x201U)

#define DMS_STATUS_ALERT_LEVEL(payload) ((payload)[0] & 0x0F)

static uint8_t s_lastAlertLevel = 0;
static uint32_t s_dmsStatusCount = 0;
static uint32_t s_dmsStatusTxFailCount = 0;

// ---- FDCAN1 --------------------------------------------------------------
//
// CAN.begin(CanBitRate::BR_500k) (see setup()) handles device_init() for
// the board's `zephyr,deferred-init` FDCAN1 node, sets the bitrate to
// 500 kbit/s (CAN-001), and starts the controller in classic (non-FD)
// mode, all internally. Deliberately no addReceiveFilter() call: the
// ZephyrCAN wrapper only tracks ONE standard (11-bit) filter at a time (a
// subsequent call replaces the previous one, per CAN.h's own doc comment)
// -- since this sketch needs 3 different standard IDs (VCS_STATUS/
// VCS_EVENT/EMERGENCY_STOP), it's simpler and just as correct to accept
// every standard frame (the library's own documented no-filter behaviour)
// and dispatch on `id` inside OnCanReceive() below, same as
// vcs-mcxn947/src/can_link/can_link.c dispatching by mailbox index.
//
// CAN.onReceive()'s callback "is executed in a dedicated worker thread,
// never in ISR context" (CAN.h's own doc comment) -- unlike a raw Zephyr
// can_rx_callback_t, it is safe to call Bridge.call()/Serial directly from
// inside OnCanReceive() below, no ISR-minimal/poll-from-loop() split
// needed here (contrast with can_link.c's ISR callback, which really is
// ISR context and really does need that split).
static uint32_t s_vcsStatusRxCount = 0;
static uint8_t s_lastDutyLeft = 0;
static uint8_t s_lastDutyRight = 0;

static void OnCanReceive(CanFDMsg const &msg, void *user_data) {
  (void)user_data;
  uint32_t id = msg.getStandardId();

  if (id == CANID_VCS_STATUS && msg.data_length == 8) {
    s_vcsStatusRxCount++;
    s_lastDutyLeft  = msg.data[2] & 0x7F;
    s_lastDutyRight = msg.data[3] & 0x7F;
    std::vector<uint8_t> payload(msg.data, msg.data + msg.data_length);
    Bridge.call("vcs_status", payload);
  } else if (id == CANID_VCS_EVENT && msg.data_length == 2) {
    std::vector<uint8_t> payload(msg.data, msg.data + msg.data_length);
    Bridge.call("vcs_event", payload);
  } else if (id == CANID_EMERGENCY_STOP && msg.data_length == 2) {
    // ap_rt_transport.py has no Bridge.provide("vcs_estop", ...) handler
    // yet (RouterBridgeTransport only registers vcs_status/vcs_event) --
    // known, not-yet-closed gap, not silently invented here. Surfaced on
    // Serial so it's visible either way; wire a "vcs_estop"
    // Bridge.provide() on the Python side too once that's needed for real
    // (see ../README.md).
    Serial.print("[can] EMERGENCY_STOP received from VCS, reason=");
    Serial.println(msg.data[0]);
  }
  // Anything else (our own EMERGENCY_STOP TX looped back, or an unrelated
  // ID) is silently ignored -- same discard-don't-guess rule as CAN-011.
}

static bool CanSendFrame(uint32_t canId, const uint8_t *data, uint8_t dlc) {
  CanMsg msg(CanStandardId(canId), dlc, data);
  int rc = CAN.write(msg);
  return rc > 0;  // CAN.h: "1 if the message was enqueued, error code < 0 if not"
}

// ---- Bridge receive handlers ----------------------------------------------
// Same confirmed pattern as the canonical sketch.ino. Payload arrives
// already ICD-encoded (icd.py's encode_*() ran before Bridge.call() on the
// Python side, see ap_rt_transport.py's _send()) -- this sketch relays the
// bytes onto FDCAN1 as-is, it does not re-encode.

void on_dms_status(std::vector<uint8_t> payload) {
  if (payload.size() != 8) {
    return;  // DLC mismatch -- same discard-don't-guess rule as CAN-011
  }
  s_lastAlertLevel = DMS_STATUS_ALERT_LEVEL(payload.data());
  s_dmsStatusCount++;

  if (!CanSendFrame(CANID_DMS_STATUS, payload.data(), 8)) {
    s_dmsStatusTxFailCount++;
  }

  // Proof-of-life kept from the canonical sketch: LED steps with alert
  // level even with no CAN peer attached.
  digitalWrite(LED_BUILTIN, s_lastAlertLevel > 0 ? LOW : HIGH);  // active-low
}

void on_dms_metrics(std::vector<uint8_t> payload) {
  if (payload.size() != 8) {
    return;
  }
  (void)CanSendFrame(CANID_DMS_METRICS, payload.data(), 8);
}

void on_emergency_stop(std::vector<uint8_t> payload) {
  if (payload.size() != 2 || payload[1] != 0x5A) {
    return;  // bad magic -- CAN-051, ignored on purpose
  }
  // The one message that must not wait for anything -- send immediately,
  // not batched with the periodic DMS_STATUS path above.
  (void)CanSendFrame(CANID_EMERGENCY_STOP, payload.data(), 2);
}

// ---- setup / loop -----------------------------------------------------------

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // active-low LED: HIGH = off, matches L0

  Serial.begin(115200);
  // Deliberately NOT `while (!Serial) {}` here, unlike CAN.h's own
  // CANRead/CANWrite examples -- those are standalone sketches where
  // blocking until a USB serial monitor attaches is harmless. This sketch
  // also has to bring up the Bridge link, which an App Lab-deployed,
  // unattended run needs regardless of whether anything is watching
  // Serial.

  if (!CAN.begin(CanBitRate::BR_500k)) {
    Serial.println("[can] CAN.begin(BR_500k) failed -- Bridge link still comes up, "
                    "but nothing will reach the physical bus");
  } else {
    CAN.onReceive(OnCanReceive, nullptr);
    Serial.println("[can] FDCAN1 up at 500 kbit/s (classic CAN)");
  }

  Bridge.begin();
  Bridge.provide("dms_status", on_dms_status);
  Bridge.provide("dms_metrics", on_dms_metrics);
  Bridge.provide("emergency_stop", on_emergency_stop);
}

void loop() {
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    Serial.print("[stats] dms_status rx=");
    Serial.print(s_dmsStatusCount);
    Serial.print(" can_tx_fail=");
    Serial.print(s_dmsStatusTxFailCount);
    Serial.print(" vcs_status_rx=");
    Serial.print(s_vcsStatusRxCount);
    Serial.print(" dutyL=");
    Serial.print(s_lastDutyLeft);
    Serial.print("% dutyR=");
    Serial.print(s_lastDutyRight);
    Serial.println("%");
  }
}
