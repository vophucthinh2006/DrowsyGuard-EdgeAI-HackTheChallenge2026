# `docs/benchmarks/` — raw measurement artefacts

Per [specs/08-benchmark-log.md](../../specs/08-benchmark-log.md) §1 (BM-002): every number quoted
in that document must cite a run ID with a matching artefact directory here,
`docs/benchmarks/<run-id>/`, containing the raw capture, the log, the git SHA, the SHA-256 of
`thresholds.yaml`, and the environment record. Run ID format: `BM-YYYY-MM-DD-NN`
([spec 08 §2](../../specs/08-benchmark-log.md#2-run-id-convention)).

**Empty today, correctly.** [Spec 08 §10](../../specs/08-benchmark-log.md#10-acceptance-criteria-scoreboard)
records the current state plainly: **0 of 15 acceptance criteria have been measured.** Every figure
quoted anywhere in this project (the pitch deck, the specs, the two READMEs) is a target or a
budget, not a result, until a directory exists here backing it up. Do not add a number to spec 08
without adding the directory that proves it first.
