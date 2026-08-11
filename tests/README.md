# `tests/` — cross-node integration + HIL

Per [specs/02-development-standards.md §2](../specs/02-development-standards.md#2-repository-layout),
this directory is for tests that exercise **both nodes together** — DMS↔VCS over a real or injected
CAN link — as opposed to the per-node unit tests that already exist and pass:

- `dms-ap/tests/` — 61 tests, domains/fusion/link, no hardware needed (`PYTHONPATH=src pytest`)
- (no equivalent yet on the `vcs-mcxn947/` firmware side — C unit tests for `safety.c`'s state
  machine would belong there, not here)

**Nothing is here yet, deliberately** — it would need one of:

1. **A CAN frame injector** (`tools/can_inject.py`, referenced in
   [specs/06 §3.2](../specs/06-test-plan.md#3-test-environment) and
   [specs/07](../specs/07-test-cases.md) but not yet written) driving `vcs-mcxn947` through every
   `alert_level` with no DMS attached — this is buildable today, `vcs-mcxn947`'s CAN0 link already
   works in isolation (see its README), the tool just hasn't been written.
2. **Real two-node hardware-in-the-loop (HIL)** — both boards, a real CAN bus, per
   [specs/04 §10 bring-up checklist](../specs/04-interface-control-document.md#10-bring-up-checklist)
   and the TC-CAN-*/TC-SAF-* cases in [specs/07](../specs/07-test-cases.md) — which needs the
   physical hardware in hand.

Do not add ad-hoc scripts here without a specs/07 TC-* ID they implement; that ID is what makes a
test in this directory legible six months from now.
