# `link/` — DMS-AP ↔ DMS-RT, and the open question underneath it

## What's real here

`icd.py` and `crc8.py` are a complete, correct, independently-tested implementation of
[`shared/icd/icd.yaml`](../../../../shared/icd/icd.yaml) and
[specs/04-interface-control-document.md](../../../../specs/04-interface-control-document.md) —
byte-identical to `vcs-mcxn947/src/icd/icd.h` / `crc8.c` on the VCS side, same CRC-8 test vectors
on both sides (`tests/test_crc8.py`, `tests/test_icd.py`, copied by hand from
[`shared/icd/crc_vectors.csv`](../../../../shared/icd/crc_vectors.csv)). See
[`shared/icd/README.md`](../../../../shared/icd/README.md) for the honest state of "shared" here:
`icd.yaml` is the documented canonical source, but `generate.py` doesn't exist yet, so this file
and the C header are still two independently hand-written implementations, not two outputs of one
generator.

## What's a stub, and why

`ap_rt_transport.py` defines the *interface* the rest of this app needs (send a `DmsStatus`,
poll for a `VcsStatus`) but has no working implementation, only `NullTransport` (does nothing).

Here's the architectural fact that makes that unavoidable right now: per
[spec 04 §1.1](../../../../specs/04-interface-control-document.md#11-node-hardware-mapping), the
CAN controller in the whole DMS node is the **FDCAN1 peripheral on the STM32U585** — the
microcontroller half of the Arduino UNO Q's dual-brain architecture. The Linux side (QRB2210,
where this Python code runs) does not have a CAN peripheral of its own. So this code can never
put a frame on the DMS↔VCS bus directly — it has to hand the payload to the STM32U585
co-processor, which is a **separate firmware project (`dms-rt/`, per
[specs/02-development-standards.md §2](../../../../specs/02-development-standards.md#2-repository-layout))
that does not exist yet**, and which then does the actual `FLEXCAN`-equivalent (`FDCAN`) transmit.

The mechanism the Arduino UNO Q uses to bridge its Linux core and its MCU core — a serial link,
Arduino's "Bridge" library, RPMsg, a shared-memory ring buffer, something else — **has not been
established in this project.** Writing a concrete implementation against a guess would be exactly
the mistake [spec 02's anti-patterns section](../../../../specs/02-development-standards.md#12-anti-patterns--explicitly-forbidden)
warns against for thresholds, applied here to an interface instead: a plausible-looking module that
silently assumes an API which may not match the real hardware, discovered only once the board is
in hand.

## What to do once the UNO Q is in hand

1. Find out how the Arduino UNO Q exposes AP↔RT communication (check Arduino's own UNO Q / App Lab
   documentation first — this is likely a solved, documented mechanism, not something to reverse
   engineer).
2. Write one new class implementing `ApRtTransport` against it. Nothing in `app.py`, `fusion/`, or
   `domains/` needs to change — they only depend on the abstract interface.
3. Start the `dms-rt/` firmware project (STM32U585) that receives on the AP↔RT link and re-transmits
   on FDCAN1 — this is the DMS-side mirror of what `vcs-mcxn947/` already is for the VCS side,
   and can reuse the same ICD (`icd.h`)/CRC-8 pattern.
4. This closes [spec 04 OI-04-01](../../../../specs/04-interface-control-document.md#9-open-items)
   (whether FDCAN1 is header-reachable) as a side effect of step 3.
