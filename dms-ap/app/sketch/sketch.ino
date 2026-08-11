// DrowsyGuard DMS-RT -- STM32U585 side of the Arduino UNO Q.
//
// specs/04-interface-control-document.md §1.1: this MCU is supposed to own
// the FDCAN1 peripheral and put DMS_STATUS/DMS_METRICS/EMERGENCY_STOP on
// the physical DMS<->VCS CAN bus, and hand VCS_STATUS/VCS_EVENT back to
// Python. **That FDCAN1 part is NOT implemented below -- see the FDCAN
// section.** What IS implemented and follows a confirmed, real pattern
// (ShawnHymel/arduino_uno_q_blink_cli's sketch/sketch.ino on GitHub, not
// guessed) is the Bridge link itself: receiving the encoded ICD payloads
// from Python (python/drowsyguard/link/ap_rt_transport.py's
// RouterBridgeTransport) and proving they arrived, via the onboard LED and
// Serial, before any CAN hardware is involved. Bring up the Bridge link
// first, confirm it with the LED blinking in step with the DMS alert
// level, THEN tackle FDCAN1 -- two independent problems, don't debug them
// at the same time.
//
// SPDX-License-Identifier: BSD-3-Clause

#include "Arduino_RouterBridge.h"

// ---- ICD byte layout, mirrors ../python/drowsyguard/link/icd.py and
// vcs-mcxn947/src/icd/icd.h byte-for-byte (specs/04 §3, shared/icd/icd.yaml).
// Only the one field this sketch actually uses today (alert_level, the low
// nibble of byte 0) is decoded; the rest arrives but is not yet acted on.
#define DMS_STATUS_ALERT_LEVEL(payload) ((payload)[0] & 0x0F)

static uint8_t s_lastAlertLevel = 0;
static uint32_t s_dmsStatusCount = 0;

// ---- Bridge receive handlers ----------------------------------------------
// Registered names match RouterBridgeTransport's _send() channel names
// exactly -- if you rename one side, rename the other in the same commit.
//
// Handler signature `std::vector<uint8_t>` for the incoming payload is an
// informed guess, not a confirmed fact: sketch.yaml pulls in "MsgPack" +
// "ArxContainer" + "ArxTypeTraits" (hideakitai's Arduino MessagePack stack,
// which supports decoding a MessagePack array into a std::vector-like
// container on no-STL embedded targets), and RouterBridgeTransport sends a
// plain `list[int]` from Python -- but the exact type Arduino_RouterBridge
// binds a `Bridge.provide()` handler's parameter to has not been confirmed
// against a real device or its header. If this doesn't compile as-is,
// check Arduino_RouterBridge.h's own examples for the array-argument form.

void on_dms_status(std::vector<uint8_t> payload) {
  if (payload.size() != 8) {
    return;  // DLC mismatch -- same discard-don't-guess rule as CAN-011
  }
  s_lastAlertLevel = DMS_STATUS_ALERT_LEVEL(payload.data());
  s_dmsStatusCount++;

  // Bring-up proof-of-life: LED brightness/blink rate steps with the alert
  // level so the Bridge link's correctness is visible without a debugger or
  // a CAN analyzer. Replace with real actuation once FDCAN1 exists -- see
  // specs/05-vehicle-control-spec.md for what the VCS side already does
  // with this same field.
  digitalWrite(LED_BUILTIN, s_lastAlertLevel > 0 ? LOW : HIGH);  // active-low
}

void on_dms_metrics(std::vector<uint8_t> payload) {
  (void)payload;  // telemetry only (CAN-020) -- nothing to act on
}

void on_emergency_stop(std::vector<uint8_t> payload) {
  if (payload.size() != 2 || payload[1] != 0x5A) {
    return;  // bad magic -- CAN-051, ignored on purpose
  }
  // TODO once FDCAN1 exists: forward onto the physical bus immediately,
  // this is the one message that must never wait for a full loop() cycle.
}

// ---- FDCAN1: NOT IMPLEMENTED -----------------------------------------------
//
// specs/04 OI-04-01 (still open): whether FDCAN1 is reachable on
// header-exposed pins from a sketch on this exact board is unconfirmed.
// There is an open, unresolved Arduino forum thread on exactly this
// question ("Trying to get fdcan working on the UNO Q") as of the research
// that went into this file -- meaning this is a real open problem on
// Arduino's own platform right now, not something this project failed to
// find. Do not copy a generic STM32 CAN library (STM32_CAN, ACANFD_STM32)
// without checking it first: those target the bare-metal STM32duino core,
// and this board's sketch.yaml FQBN is `arduino:zephyr:unoq` -- a
// Zephyr-based Arduino core -- so a bare-metal peripheral-register library
// may not apply here at all. The more promising lead is whatever CAN
// support the Zephyr devicetree for this board exposes (a
// `zephyr,fdcan`-compatible node), reachable Arduino-side only if the
// arduino:zephyr core wraps it. Check the forum thread above for the
// current state before spending time on this.

// ---- setup / loop -----------------------------------------------------------

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // active-low LED: HIGH = off, matches L0

  Serial.begin(115200);

  Bridge.begin();
  Bridge.provide("dms_status", on_dms_status);
  Bridge.provide("dms_metrics", on_dms_metrics);
  Bridge.provide("emergency_stop", on_emergency_stop);

  // No startup handshake here (e.g. a "linux_started" blocking wait) --
  // the confirmed reference example (blink_cli) doesn't use one, and an
  // unverified blocking `while(!ok) { ... }` risks hanging setup() forever
  // if the exact RPC round-trip shape doesn't match what's assumed. If a
  // race between this sketch and Python's own startup shows up in
  // practice, that's the next thing to add -- with a bounded retry count,
  // not an unbounded wait.
}

void loop() {
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    Serial.print("dms_status frames received: ");
    Serial.println(s_dmsStatusCount);
  }
}
