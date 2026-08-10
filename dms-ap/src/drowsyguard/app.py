"""DMS-AP main loop: capture -> inference -> domains -> fusion -> link.

specs/02-development-standards.md DEV-045: a SIGTERM SHALL result in a clean
shutdown that publishes level=L0, calib_done=0 and disarms the VCS -- dying
without telling the vehicle is a safety defect here, not an inconvenience.
This is implemented with a signal handler that flips a flag checked once per
loop iteration, not an `atexit` hook, so the "tell the VCS" step is a normal
send through the same transport as every other frame rather than a
best-effort cleanup racing process teardown.
"""

from __future__ import annotations

import argparse
import signal
import time
from collections.abc import Callable
from pathlib import Path

import cv2

from .capture.camera import Camera
from .config import Thresholds, load_thresholds
from .domains.d1_distraction import D1Distraction
from .domains.d2_yawn import D2Yawn
from .domains.d3_eyeclosure import D3EyeClosure
from .domains.types import DomainState
from .fusion.ladder import AlertLadder, FusionInputs
from .inference.backend import InferenceBackend
from .inference.blazeface_cnn_backend import BlazeFaceCnnBackend
from .inference.debug_overlay import draw_overlay
from .link import icd
from .link.ap_rt_transport import ApRtTransport, NullTransport
from .telemetry import logger

DEFAULT_MODELS_DIR = Path(__file__).resolve().parents[2] / "models"
CALIB_WARMUP_MS = 3000.0  # SYS-ER-005 budget is <=30 s; this is well inside it


class _ShutdownRequested:
    flag = False

    def handler(self, signum: int, frame: object) -> None:
        self.flag = True


def _domain_state_to_wire(state: DomainState) -> icd.DomainWireState:
    return icd.DomainWireState(int(state))


def _run_pipeline(
    backend: InferenceBackend,
    thresholds: Thresholds,
    transport: ApRtTransport,
    *,
    preview: bool,
    time_source: Callable[[], float] = time.monotonic,
) -> None:
    d1 = D1Distraction(thresholds.d1)
    d2 = D2Yawn(thresholds.d2)
    d3 = D3EyeClosure(thresholds.d3)
    fusion = AlertLadder(thresholds.fusion)

    shutdown = _ShutdownRequested()
    signal.signal(signal.SIGTERM, shutdown.handler)
    signal.signal(signal.SIGINT, shutdown.handler)

    start_ms = time_source() * 1000.0
    seq4 = 0  # DMS_STATUS 4-bit seq
    seq8 = 0  # DMS_METRICS 8-bit seq
    last_metrics_ms = start_ms
    frame_count_since_metrics = 0
    face_lost_since_ms: float | None = None

    logger.info("app", "started", warmup_ms=CALIB_WARMUP_MS)

    try:
        while not shutdown.flag:
            now_ms = time_source() * 1000.0

            obs = backend.process(now_ms)
            if obs is None:
                continue
            frame_count_since_metrics += 1

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

            calib_done = (now_ms - start_ms) >= CALIB_WARMUP_MS

            perclos = d3.perclos
            status = icd.DmsStatus(
                alert_level=icd.AlertLevel(int(level)) if calib_done else icd.AlertLevel.L0_NORMAL,
                seq=seq4,
                d1_state=_domain_state_to_wire(d1_state),
                d2_state=_domain_state_to_wire(d2_state),
                d3_state=_domain_state_to_wire(d3_state),
                d3_avail=(
                    icd.D3Availability.UNAVAILABLE
                    if d3.unavailable
                    else icd.D3Availability.AVAILABLE
                ),
                perclos_pct=round(perclos * 100) if perclos is not None else 255,
                eye_closure_ms=min(int(d3.continuous_closed_ms(now_ms)), 65534),
                face_conf_pct=(
                    round(obs.face_confidence * 100) if obs.face_present else 255
                ),
                flag_ack_refractory=fusion.l1_suppressed(now_ms),
                flag_sensor_lost=sensor_lost,
                flag_model_degraded=d3.model_degraded,
                flag_night_mode=False,
                flag_calib_done=calib_done,
                flag_pipeline_slow=False,  # see README "What is NOT wired yet"
                flag_ack_saturated=fusion.ack_saturated,
            )
            transport.send_dms_status(status)
            seq4 = (seq4 + 1) % 16

            if now_ms - last_metrics_ms >= 500.0:
                elapsed_s = (now_ms - last_metrics_ms) / 1000.0
                fps = frame_count_since_metrics / elapsed_s if elapsed_s > 0 else 0.0
                metrics = icd.DmsMetrics(
                    fps_x10=min(round(fps * 10), 255),
                    inference_ms=0,  # see README "What is NOT wired yet"
                    yawn_count=d2.event_count,
                    eor_cum_ms=min(int(d1.cumulative_off_road_ms(now_ms)), 65535),
                    dropped_pct=0,
                    seq=seq8,
                )
                transport.send_dms_metrics(metrics)
                seq8 = (seq8 + 1) % 256
                last_metrics_ms = now_ms
                frame_count_since_metrics = 0

            for event_id, _event_seq in transport.poll_events():
                if event_id == int(icd.EventId.ACK):
                    fusion.acknowledge(now_ms)
                elif event_id == int(icd.EventId.OPERATOR_REARM):
                    fusion.resolve_emergency(now_ms)
                elif event_id == int(icd.EventId.INDICATOR_ON):
                    pass  # applied to the NEXT frame's obs.indicator_active (see README)
                elif event_id == int(icd.EventId.INDICATOR_OFF):
                    pass

            logger.info(
                "fusion",
                "tick",
                level=level.name,
                d1=d1_state.name,
                d2=d2_state.name,
                d3=d3_state.name,
                calib_done=calib_done,
            )

            if preview:
                frame = getattr(backend, "last_frame", None)
                if frame is not None:
                    draw_overlay(frame, obs, level)
                    cv2.imshow("DrowsyGuard DMS-AP preview", frame)
                    if cv2.waitKey(1) & 0xFF == ord("q"):
                        break
    finally:
        logger.info("app", "shutting_down")
        # DEV-045: publish L0 + calib_done=0 and let the VCS's own
        # CAN-063 "absence is never L0" supervisor take over from here --
        # this is the LAST frame this process gets to send, not a promise
        # the VCS will keep listening to it.
        shutdown_status = icd.DmsStatus(
            alert_level=icd.AlertLevel.L0_NORMAL,
            seq=seq4,
            d1_state=icd.DomainWireState.IDLE,
            d2_state=icd.DomainWireState.IDLE,
            d3_state=icd.DomainWireState.IDLE,
            d3_avail=icd.D3Availability.AVAILABLE,
            perclos_pct=255,
            eye_closure_ms=65535,
            face_conf_pct=255,
            flag_calib_done=False,
        )
        transport.send_dms_status(shutdown_status)
        backend.close()
        if preview:
            cv2.destroyAllWindows()


def main() -> None:
    parser = argparse.ArgumentParser(description="DrowsyGuard DMS-AP")
    parser.add_argument("--camera-index", type=int, default=0)
    parser.add_argument("--models-dir", type=Path, default=DEFAULT_MODELS_DIR)
    parser.add_argument("--thresholds", type=Path, default=None)
    parser.add_argument(
        "--preview", action="store_true", help="show a debug overlay window (bench only)"
    )
    args = parser.parse_args()

    thresholds = load_thresholds(args.thresholds) if args.thresholds else load_thresholds()
    camera = Camera(args.camera_index)
    backend = BlazeFaceCnnBackend(args.models_dir, camera)
    transport: ApRtTransport = NullTransport()  # see link/README.md

    _run_pipeline(backend, thresholds, transport, preview=args.preview)


if __name__ == "__main__":
    main()
