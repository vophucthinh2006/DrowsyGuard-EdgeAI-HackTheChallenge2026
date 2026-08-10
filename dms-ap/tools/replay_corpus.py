#!/usr/bin/env python3
"""Drives a recorded JSONL FrameObservation corpus (inference/replay_backend.py
format) through the exact production domains+fusion pipeline and prints the
level timeline. This is the mechanism specs/06-test-plan.md §2 describes as
"the highest-value test in the project" -- a scaffold for it, not the real
tools/eval_corpus.py from specs 06 §4/07, which needs an actual annotated
corpus (specs 06 §5) that does not exist yet.

Usage:
    python tools/replay_corpus.py path/to/corpus.jsonl
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from drowsyguard.config import load_thresholds  # noqa: E402
from drowsyguard.domains.d1_distraction import D1Distraction  # noqa: E402
from drowsyguard.domains.d2_yawn import D2Yawn  # noqa: E402
from drowsyguard.domains.d3_eyeclosure import D3EyeClosure  # noqa: E402
from drowsyguard.fusion.ladder import AlertLadder, FusionInputs  # noqa: E402
from drowsyguard.inference.replay_backend import ReplayBackend  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("--thresholds", type=Path, default=None)
    args = parser.parse_args()

    thresholds = load_thresholds(args.thresholds) if args.thresholds else load_thresholds()

    d1 = D1Distraction(thresholds.d1)
    d2 = D2Yawn(thresholds.d2)
    d3 = D3EyeClosure(thresholds.d3)
    fusion = AlertLadder(thresholds.fusion)

    face_lost_since_ms: float | None = None
    prev_level = None
    frame_count = 0

    for obs in ReplayBackend(args.corpus).frames():
        frame_count += 1
        now_ms = obs.timestamp_ms

        if not obs.face_present:
            if face_lost_since_ms is None:
                face_lost_since_ms = now_ms
        else:
            face_lost_since_ms = None
        sensor_lost = (
            face_lost_since_ms is not None
            and (now_ms - face_lost_since_ms) >= thresholds.fault.sensor_lost_ms
        )

        d1_state = d1.update(obs, now_ms)
        d2_state = d2.update(obs, now_ms)
        d3_state = d3.update(obs, now_ms)
        level = fusion.update(
            FusionInputs(
                d1=d1_state,
                d2=d2_state,
                d3=d3_state,
                sensor_lost=sensor_lost,
                d3_unavailable=d3.unavailable,
            ),
            now_ms,
        )

        if level != prev_level:
            print(
                f"t={now_ms:>10.0f}ms  level={level.name:<10} "
                f"d1={d1_state.name:<8} d2={d2_state.name:<8} d3={d3_state.name:<8}"
                f"{'  SENSOR_LOST' if sensor_lost else ''}"
            )
            prev_level = level

    print(f"\n{frame_count} frames replayed.")


if __name__ == "__main__":
    main()
