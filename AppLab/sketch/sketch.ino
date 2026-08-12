// SPDX-License-Identifier: MIT
//
// This app does not use the STM32/Zephyr (MCU) side at all - all logic
// (camera capture, TFLite inference, drowsiness/obstruction detection,
// web viewer) runs in python/main.py on the Linux (MPU) side.
//
// Arduino App Lab's project structure still expects a sketch/ folder with
// an entrypoint, so this is kept as an empty, do-nothing sketch.

void setup() {
  // Intentionally left blank.
}

void loop() {
  // Intentionally left blank.
}
