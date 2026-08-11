# `shared/icd/` — the DMS↔VCS interface, in one place

Per [specs/02-development-standards.md §2](../../specs/02-development-standards.md#2-repository-layout)
(DEV-001/DEV-002), this directory is supposed to be **the only place CAN message layouts are
written** — `icd.yaml` here, with `generate.py` emitting the C header, the Python module and a
`.dbc` from it, so the two firmware/software implementations *cannot* disagree.

## Current state — honest, not aspirational

- **`icd.yaml`** exists and is complete: every message, field, bit offset and enum value from
  [specs/04-interface-control-document.md](../../specs/04-interface-control-document.md), in one
  machine-readable file.
- **`crc_vectors.csv`** exists and is the canonical CRC-8 test data (CAN-070).
- **`generate.py` does not exist.** The two real implementations —
  [`vcs-mcxn947/src/icd/icd.h`+`icd.c`](../../vcs-mcxn947/src/icd/) (C) and
  [`dms-ap/app/python/drowsyguard/link/icd.py`](../../dms-ap/app/python/drowsyguard/link/icd.py) (Python) — are
  **hand-written**, each directly from spec 04, and cross-checked only by eye and by both reading
  the same `crc_vectors.csv` values (copied into each language's own test/self-test code, since
  neither runtime can read a CSV from flash or wants a CSV dependency for one constant table).

This is weaker than DEV-002's actual goal. "Both sides read `icd.yaml` and cannot disagree" is not
true yet — it is "both sides were written from the same spec and a human checked the diff." A
future field change made carelessly in one file and not the other would compile fine on both sides
and fail silently on the bus.

## What closing this properly looks like

1. `generate.py`: parse `icd.yaml`, emit `vcs-mcxn947/src/icd/icd.h` and
   `dms-ap/app/python/drowsyguard/link/icd.py` byte-for-byte identical to what's committed today (first
   run should produce a **zero diff** against the current hand-written files — that's the
   correctness check for the generator itself).
2. Wire `generate.py --check` into CI (DEV-003): re-run it, fail the build on any diff. This is
   what actually makes "the ICD cannot drift" true instead of hoped-for.
3. Optionally emit a `drowsyguard.dbc` for use with standard CAN tooling (`cantools`, SavvyCAN,
   PCAN-View) during bring-up — not required for the firmware/software to work, but valuable for
   [specs/04 §10 bring-up checklist](../../specs/04-interface-control-document.md#10-bring-up-checklist)
   and for `tools/can_inject.py` (referenced in specs 06/07, not yet written).

Until step 1 exists, **any ICD change must be applied to `icd.yaml` AND both hand-written files in
the same commit** (specs/02 DEV-092), and the reviewer checking that PR should diff all three by
hand.
