# `link/` — DMS-AP ↔ DMS-RT, and what's real vs. still open

## What's real here

`icd.py` and `crc8.py` are a complete, correct, independently-tested implementation of
[`shared/icd/icd.yaml`](../../../../../shared/icd/icd.yaml) and
[specs/04-interface-control-document.md](../../../../../specs/04-interface-control-document.md) —
byte-identical to `vcs-mcxn947/src/icd/icd.h` / `crc8.c` on the VCS side, same CRC-8 test vectors
on both sides (`tests/test_crc8.py`, `tests/test_icd.py`, copied by hand from
[`shared/icd/crc_vectors.csv`](../../../../../shared/icd/crc_vectors.csv)). See
[`shared/icd/README.md`](../../../../../shared/icd/README.md) for the honest state of "shared"
here: `icd.yaml` is the documented canonical source, but `generate.py` doesn't exist yet, so this
file and the C header are still two independently hand-written implementations, not two outputs of
one generator.

`ap_rt_transport.py`'s `RouterBridgeTransport` is a real implementation of the AP↔RT link on top
of **Arduino_RouterBridge** — see below for what that is and how confident to be in it.

## The AP↔RT mechanism: Arduino_RouterBridge (resolved)

This used to say "the mechanism has not been established in this project." It has been found: the
Arduino UNO Q's Linux (MPU) side and STM32U585 (MCU) side talk over the **Router Bridge**,
MessagePack-RPC over the internal serial line, brokered by App Lab's own router service
([arduino/arduino-router](https://github.com/arduino/arduino-router) on GitHub). This is a
first-class, documented part of the platform, not a workaround.

Confirmed API (fetched from a real, published App Lab app —
[ShawnHymel/arduino_uno_q_blink_cli](https://github.com/ShawnHymel/arduino_uno_q_blink_cli) on
GitHub, its actual `python/main.py` and `sketch/sketch.ino` content, not a paraphrase):

```python
# python side -- from arduino.app_utils import *
Bridge.provide(name, python_function)   # sketch calls this by name
Bridge.call(name, data)                 # calls a function the sketch registered
App.run(user_loop=loop)                 # App Lab's process entry pattern
```
```cpp
// sketch side -- #include "Arduino_RouterBridge.h"
Bridge.begin();
Bridge.provide(name, cpp_function);
Bridge.call(name, data);
```

`RouterBridgeTransport` in `ap_rt_transport.py` implements `ApRtTransport` on this: `send_*()`
methods call `icd.py`'s encoders then `Bridge.call()` the encoded bytes as a `list[int]` to the
sketch; `poll_vcs_status()`/`poll_events()` drain queues filled by `Bridge.provide()`-registered
callbacks that the sketch invokes.

**What is genuinely unverified, listed precisely rather than glossed over:**
- Whether `Bridge.call()` will accept and faithfully round-trip a `list[int]` of exactly 8 elements
  (an ICD frame's payload) the same way it round-trips the primitives (`bool`, presumably numbers)
  shown in the one confirmed example. MessagePack itself supports arrays fine; whether
  Arduino_RouterBridge's own binding layer does anything unexpected with one is untested.
- The exact C++ type a `Bridge.provide()` handler's parameter binds to for an incoming array
  (`sketch.ino` guesses `std::vector<uint8_t>` based on the sketch's MsgPack/ArxContainer library
  dependencies — an informed guess, not a confirmed one).
- Whether `App.run(user_loop=loop)` gives any way to run cleanup code on stop (no stop/cleanup hook
  appears in the one confirmed example) — `main.py`'s `run_app_lab()` falls back to a SIGTERM
  handler for this, itself unverified against how App Lab actually stops a running app.
- Everything above this line, on a real device. None was available while writing this.

## What's still genuinely missing: FDCAN1

Per [spec 04 §1.1](../../../../../specs/04-interface-control-document.md#11-node-hardware-mapping),
DMS-RT (the sketch) is supposed to own the **FDCAN1** peripheral and put frames on the physical
DMS↔VCS CAN bus. `../../../sketch/sketch.ino` receives Bridge messages and proves the link works
(LED + Serial), but does **not** touch FDCAN1 — that part is unimplemented.

This is not a gap this project failed to research: there is an **open, unresolved thread on
Arduino's own forum** ("Trying to get fdcan working on the UNO Q") as of the research that went
into this file, meaning FDCAN1-from-a-sketch on this exact board is an open problem on the
platform right now. Two things worth knowing before attempting it:
- `sketch.yaml`'s FQBN is `arduino:zephyr:unoq` — a **Zephyr-based** Arduino core. Generic
  bare-metal STM32 CAN Arduino libraries (STM32_CAN, ACANFD_STM32) target the STM32duino core and
  may not apply to this board at all. The more promising lead is whatever the Zephyr devicetree
  for this board exposes (a `zephyr,fdcan`-compatible node), if and only if the `arduino:zephyr`
  core wraps it for sketch use.
- This also means [spec 04 OI-04-01](../../../../../specs/04-interface-control-document.md#9-open-items)
  (whether FDCAN1 is header-reachable at all) stays open until this is resolved.

## What to do next, in order

1. Get a real UNO Q, load `dms-ap/app/` in App Lab, confirm the Bridge link (watch the LED step
   with `alert_level`, watch Serial for the frame counter) before touching CAN at all.
2. Fix whichever of the "genuinely unverified" assumptions above turned out wrong — each is a
   one-file change (either `ap_rt_transport.py`'s `_send()`/callback types, or `sketch.ino`'s
   handler signatures), not a redesign.
3. Resolve FDCAN1 (see the forum thread above for the current state of that problem), then relay
   the already-decoded ICD frames onto the physical bus. This closes spec 04 OI-04-01 as a side
   effect.
