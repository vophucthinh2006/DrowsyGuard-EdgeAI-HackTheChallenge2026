# Summary of 08 — Benchmark Log

Source: [specs/08-benchmark-log.md](../../specs/08-benchmark-log.md)

## Role of this document
This is the **log of real measurements**, distinct from specs 01/06 (which contain
budgets/targets). At Rev 0.1: **NO MEASUREMENTS RECORDED YET** — every table is `_pending_`.
This is where numbers get filled in once real hardware exists, not a place to copy predicted
numbers from other specs.

## Rules for recording numbers (strict — worth remembering)
- Only record numbers that were **actually measured**, never targets/budgets/estimates.
- Every entry needs a **run ID** (`BM-YYYY-MM-DD-NN`) + an artefact directory
  `docs/benchmarks/<run-id>/` with the raw capture, log, git SHA, `thresholds.yaml` SHA-256,
  environment record.
- **Never** from a `+dirty` build.
- Latency/timing always reported as **P50/P95/max**, never mean (mean hides the tail — and the
  tail is exactly what a drowsy driver experiences).
- Detection metrics always reported **as a pair**: TPR **and** false-alarm rate at the same
  operating point — a TPR alone is an incomplete result.
- When a result misses its target: still record it **as measured**, with a note on the
  suspected cause. Re-running until a good number appears and only recording that one is
  **data fabrication**, explicitly forbidden.
- Every result names the person who took it (not for blame — so the person who knows what the
  rig looked like that day can be asked).

## Table structure (all currently empty, to be filled with hardware)
Latency & timing (pipeline P50/P95/max, stage breakdown, driver-experienced time-to-alert,
control-loop jitter) · Inference throughput (sustained FPS, thermal behaviour, RAM/flash
footprint) · Detection quality (headline operating point AC-01..04, per-domain detail,
**false-alarm breakdown by cause** — noted as "the most useful table in this document" since it
shows which threshold is wrong, not just the total) · Interface (CAN physical, 30-min bus
health, timeout behaviour) · Vehicle (drivetrain, speed governing, safe-stop, alerts) · Power ·
Scoreboard of 15 acceptance criteria (**currently 0/15 measured**) · Anomaly register (every
unexpected observation, even ones later explained away — the ones written down are the ones that
get fixed) · **Numbers currently quoted in external material**.

## Most important section: "Numbers currently quoted in external material"
A table cross-checking every number used in the pitch deck/script/submission against whether
it's backed by a real measurement:
- "under 200ms end-to-end" → **Target**, not measured — must say "our target is," replace with
  real measured P95 once AC-05 is run.
- "under 100ms on the MCU" (deck slide 11) → **Target**; the actual spec budget is 20ms.
- **"3 seconds = 100 meters" (script 0:08, 4:40) — CURRENTLY WRONG, UNFIXED:** 100m in 3s
  corresponds to 120km/h, but the script says 90km/h → the correct number should be **75
  meters**. Exactly the kind of error "a judge with a calculator will check" — needs fixing
  (either the speed to 120km/h or the distance to 75m) before recording any video/presentation.
- "nano INT8 at ≈N FPS" (deck slide 20) → **unfilled placeholder**, must fill from AC-06 or delete the slide.
- "TRL 4" → reasonable self-assessment now, only becomes TRL 5-6 once AC-01…15 are measured on
  real hardware.

**This table must be reviewed before any video/demo/presentation is recorded.**
