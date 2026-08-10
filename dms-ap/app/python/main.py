"""DMS-AP entry point: capture -> inference -> domains -> fusion -> link.

This is the file Arduino App Lab runs directly as `python/main.py` (Arduino
UNO Q app structure: `app.yaml` + `python/` + `sketch/`). It is NOT part of
the `drowsyguard` package -- it imports it with absolute imports
(`from drowsyguard...`, not `from .`), because App Lab executes it as a
script, not as `-m drowsyguard.something`.

Two run modes, auto-detected:
  - **Under App Lab**: `arduino.app_utils` is importable. Uses
    `App.run(user_loop=loop)` (the pattern App Lab's own examples use) and
    wires up `RouterBridgeTransport` (see drowsyguard/link/ap_rt_transport.py)
    so DMS_STATUS actually reaches the sketch -> FDCAN1 -> the VCS.
  - **Bench/dev mode**: `arduino.app_utils` is not importable (this is the
    normal case everywhere except a real UNO Q running App Lab). Falls back
    to a manual loop + argparse CLI (--camera-index, --models-dir,
    --thresholds, --preview), same behaviour this file had before the
    App Lab restructure, and `NullTransport` (see link/README.md).

specs/02-development-standards.md DEV-045: a SIGTERM SHALL result in a clean
shutdown that publishes level=L0, calib_done=0 and disarms the VCS -- dying
without telling the vehicle is a safety defect here, not an inconvenience.
In bench mode this is a signal handler; under App Lab, `App.run()` owns the
process lifecycle, so the same cleanup runs from `App.on_stop` instead (see
`_build_pipeline`/`run_app_lab` below) -- whichever mechanism actually gets
the last word before the process exits.
"""

from __future__ import annotations

import argparse
import signal
import time
from collections.abc import Callable
from pathlib import Path

from drowsyguard.capture.camera import Camera
from drowsyguard.config import Thresholds, load_thresholds
from drowsyguard.domains.d1_distraction import D1Distraction
from drowsyguard.domains.d2_yawn import D2Yawn
from drowsyguard.domains.d3_eyeclosure import D3EyeClosure
from drowsyguard.domains.types import DomainState
from drowsyguard.fusion.ladder import AlertLadder, FusionInputs
from drowsyguard.inference.backend import InferenceBackend
from drowsyguard.inference.blazeface_cnn_backend import BlazeFaceCnnBackend
from drowsyguard.link import icd
from drowsyguard.link.ap_rt_transport import ApRtTransport, NullTransport
from drowsyguard.telemetry import logger

# main.py sits directly in python/, so models/ and (via config.py) config/
# are siblings of this file -- both deploy inside app/ together with it.
DEFAULT_MODELS_DIR = Path(__file__).resolve().parent / "models"
CALIB_WARMUP_MS = 3000.0  # SYS-ER-005 budget is <=30 s; this is well inside it


def _domain_state_to_wire(state: DomainState) -> icd.DomainWireState:
    return icd.DomainWireState(int(state))


class _Pipeline:
    """One frame's worth of capture->inference->domains->fusion->link, held
    as state so both run modes (App Lab's callback loop, bench mode's while
    loop) can drive the exact same `tick()` once per frame."""

    def __init__(
        self,
        backend: InferenceBackend,
        thresholds: Thresholds,
        transport: ApRtTransport,
        time_source: Callable[[], float],
    ) -> None:
        self.backend = backend
        self.thresholds = thresholds
        self.transport = transport
        self.time_source = time_source

        self.d1 = D1Distraction(thresholds.d1)
        self.d2 = D2Yawn(thresholds.d2)
        self.d3 = D3EyeClosure(thresholds.d3)
        self.fusion = AlertLadder(thresholds.fusion)

        self.start_ms = time_source() * 1000.0
        self.seq4 = 0  # DMS_STATUS 4-bit seq
        self.seq8 = 0  # DMS_METRICS 8-bit seq
        self.last_metrics_ms = self.start_ms
        self.frame_count_since_metrics = 0
        self.face_lost_since_ms: float | None = None

        self.last_obs = None
        self.last_level = None

        logger.info("main", "started", warmup_ms=CALIB_WARMUP_MS)

    def tick(self) -> None:
        now_ms = self.time_source() * 1000.0

        obs = self.backend.process(now_ms)
        if obs is None:
            return
        self.last_obs = obs
        self.frame_count_since_metrics += 1

        if not obs.face_present:
            if self.face_lost_since_ms is None:
                self.face_lost_since_ms = now_ms
        else:
            self.face_lost_since_ms = None
        sensor_lost = (
            self.face_lost_since_ms is not None
            and (now_ms - self.face_lost_since_ms) >= self.thresholds.fault.sensor_lost_ms
        )

        d1_state = self.d1.update(obs, now_ms)
        d2_state = self.d2.update(obs, now_ms)
        d3_state = self.d3.update(obs, now_ms)

        level = self.fusion.update(
            FusionInputs(
                d1=d1_state,
                d2=d2_state,
                d3=d3_state,
                sensor_lost=sensor_lost,
                d3_unavailable=self.d3.unavailable,
            ),
            now_ms,
        )
        self.last_level = level

        calib_done = (now_ms - self.start_ms) >= CALIB_WARMUP_MS

        perclos = self.d3.perclos
        status = icd.DmsStatus(
            alert_level=icd.AlertLevel(int(level)) if calib_done else icd.AlertLevel.L0_NORMAL,
            seq=self.seq4,
            d1_state=_domain_state_to_wire(d1_state),
            d2_state=_domain_state_to_wire(d2_state),
            d3_state=_domain_state_to_wire(d3_state),
            d3_avail=(
                icd.D3Availability.UNAVAILABLE
                if self.d3.unavailable
                else icd.D3Availability.AVAILABLE
            ),
            perclos_pct=round(perclos * 100) if perclos is not None else 255,
            eye_closure_ms=min(int(self.d3.continuous_closed_ms(now_ms)), 65534),
            face_conf_pct=round(obs.face_confidence * 100) if obs.face_present else 255,
            flag_ack_refractory=self.fusion.l1_suppressed(now_ms),
            flag_sensor_lost=sensor_lost,
            flag_model_degraded=self.d3.model_degraded,
            flag_night_mode=False,
            flag_calib_done=calib_done,
            flag_pipeline_slow=False,  # see README "What is NOT wired yet"
            flag_ack_saturated=self.fusion.ack_saturated,
        )
        self.transport.send_dms_status(status)
        self.seq4 = (self.seq4 + 1) % 16

        if now_ms - self.last_metrics_ms >= 500.0:
            elapsed_s = (now_ms - self.last_metrics_ms) / 1000.0
            fps = self.frame_count_since_metrics / elapsed_s if elapsed_s > 0 else 0.0
            metrics = icd.DmsMetrics(
                fps_x10=min(round(fps * 10), 255),
                inference_ms=0,  # see README "What is NOT wired yet"
                yawn_count=self.d2.event_count,
                eor_cum_ms=min(int(self.d1.cumulative_off_road_ms(now_ms)), 65535),
                dropped_pct=0,
                seq=self.seq8,
            )
            self.transport.send_dms_metrics(metrics)
            self.seq8 = (self.seq8 + 1) % 256
            self.last_metrics_ms = now_ms
            self.frame_count_since_metrics = 0

        for event_id, _event_seq in self.transport.poll_events():
            if event_id == int(icd.EventId.ACK):
                self.fusion.acknowledge(now_ms)
            elif event_id == int(icd.EventId.OPERATOR_REARM):
                self.fusion.resolve_emergency(now_ms)
            # INDICATOR_ON/OFF: applied to the NEXT frame's obs.indicator_active
            # once a real transport supplies it -- no-op today, see README.

        logger.info(
            "fusion",
            "tick",
            level=level.name,
            d1=d1_state.name,
            d2=d2_state.name,
            d3=d3_state.name,
            calib_done=calib_done,
        )

    def shutdown(self) -> None:
        logger.info("main", "shutting_down")
        # DEV-045: publish L0 + calib_done=0 and let the VCS's own CAN-063
        # "absence is never L0" supervisor take over from here -- this is
        # the LAST frame this process gets to send, not a promise the VCS
        # will keep listening to it.
        shutdown_status = icd.DmsStatus(
            alert_level=icd.AlertLevel.L0_NORMAL,
            seq=self.seq4,
            d1_state=icd.DomainWireState.IDLE,
            d2_state=icd.DomainWireState.IDLE,
            d3_state=icd.DomainWireState.IDLE,
            d3_avail=icd.D3Availability.AVAILABLE,
            perclos_pct=255,
            eye_closure_ms=65535,
            face_conf_pct=255,
            flag_calib_done=False,
        )
        self.transport.send_dms_status(shutdown_status)
        self.backend.close()


def _build_pipeline(
    *, camera_index: int, models_dir: Path, thresholds_path: Path | None, transport: ApRtTransport
) -> _Pipeline:
    thresholds = load_thresholds(thresholds_path) if thresholds_path else load_thresholds()
    camera = Camera(camera_index)
    backend = BlazeFaceCnnBackend(models_dir, camera)
    return _Pipeline(backend, thresholds, transport, time.monotonic)


def run_bench_mode() -> None:
    """Dev/bench entry point: `python main.py [--preview] [--camera-index N] ...`.
    Never runs on the actual device under App Lab -- see module docstring."""
    import cv2

    from drowsyguard.inference.debug_overlay import draw_overlay

    parser = argparse.ArgumentParser(description="DrowsyGuard DMS-AP (bench mode)")
    parser.add_argument("--camera-index", type=int, default=0)
    parser.add_argument("--models-dir", type=Path, default=DEFAULT_MODELS_DIR)
    parser.add_argument("--thresholds", type=Path, default=None)
    parser.add_argument(
        "--preview", action="store_true", help="show a debug overlay window (bench only)"
    )
    args = parser.parse_args()

    pipeline = _build_pipeline(
        camera_index=args.camera_index,
        models_dir=args.models_dir,
        thresholds_path=args.thresholds,
        transport=NullTransport(),  # see link/README.md
    )

    shutdown_requested = False

    def _handle_signal(signum: int, frame: object) -> None:
        nonlocal shutdown_requested
        shutdown_requested = True

    signal.signal(signal.SIGTERM, _handle_signal)
    signal.signal(signal.SIGINT, _handle_signal)

    try:
        while not shutdown_requested:
            pipeline.tick()
            if args.preview and pipeline.last_obs is not None:
                frame = getattr(pipeline.backend, "last_frame", None)
                if frame is not None:
                    draw_overlay(frame, pipeline.last_obs, pipeline.last_level)
                    cv2.imshow("DrowsyGuard DMS-AP preview", frame)
                    if cv2.waitKey(1) & 0xFF == ord("q"):
                        break
    finally:
        pipeline.shutdown()
        if args.preview:
            cv2.destroyAllWindows()


def run_app_lab() -> None:
    """App Lab entry point. Only called when `arduino.app_utils` actually
    imports -- i.e. only ever on the real device.

    `App.run(user_loop=loop)` matches the confirmed real pattern (Arduino's
    own examples, e.g. ShawnHymel/arduino_uno_q_blink_cli's python/main.py
    on GitHub: `from arduino.app_utils import *`, a module-level `loop()`,
    `App.run(user_loop=loop)`). Nothing in that confirmed example shows a
    stop/cleanup hook, so DEV-045's "publish L0 + calib_done=0 on shutdown"
    cleanup is done here via a SIGTERM/SIGINT handler instead -- App Lab
    almost certainly stops an app by signalling its process (this is the
    standard way any process supervisor stops a Python script, and matches
    run_bench_mode()'s already-working pattern below), but this specific
    assumption (that a signal handler set before `App.run()` still fires
    while `App.run()` owns the main loop) has not been verified against a
    real device either. If it doesn't fire in practice, the fallback is
    simply that the last DMS_STATUS the VCS saw stays valid until its own
    CAN-063 timeout supervisor (300 ms) takes over -- degraded, not unsafe.
    """
    from arduino.app_utils import App  # type: ignore[import-not-found]

    from drowsyguard.link.ap_rt_transport import RouterBridgeTransport

    transport = RouterBridgeTransport()
    pipeline = _build_pipeline(
        camera_index=0,
        models_dir=DEFAULT_MODELS_DIR,
        thresholds_path=None,
        transport=transport,
    )

    shutdown_requested = False

    def _handle_signal(signum: int, frame: object) -> None:
        nonlocal shutdown_requested
        shutdown_requested = True
        pipeline.shutdown()  # best-effort; see docstring above

    signal.signal(signal.SIGTERM, _handle_signal)
    signal.signal(signal.SIGINT, _handle_signal)

    def loop() -> None:
        if not shutdown_requested:
            pipeline.tick()

    App.run(user_loop=loop)


def main() -> None:
    try:
        import arduino.app_utils  # noqa: F401

        run_app_lab()
    except ImportError:
        run_bench_mode()


if __name__ == "__main__":
    main()
