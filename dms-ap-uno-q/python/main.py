"""DrowsyGuard DMS-AP CAN control test -- manual alert-level driver +
simulated-speed dashboard.

Scratch/test copy of the real DMS-AP app's control path
(../../QUALCOMM_AI/DrowsyGuard-EdgeAI-HackTheChallenge2026/dms-ap/app/python/main.py),
with the camera/inference/fusion pipeline replaced by a keyboard-driven
alert level -- there's no camera in this test loop, a human is standing in
for D1/D2/D3 fusion to drive the VCS's alert-reactive behaviour on demand.
Swap `KeyboardAlertSource` for the real fusion pipeline's output later; the
transport/protocol code (`drowsyguard.link.*`) is an untouched copy of the
real, tested implementation, so nothing downstream of "what alert_level is
right now" needs to change to go from this test script to the real thing.

Two run modes, auto-detected, same pattern as the canonical main.py:
  - **Under App Lab** (arduino.app_utils importable): App.run(user_loop=loop),
    RouterBridgeTransport -> sketch.ino -> FDCAN1 -> real CAN bus.
  - **Bench/dev mode** (anywhere else, e.g. this dev machine): plain while
    loop, NullTransport -- lets you sanity-check the keyboard control logic
    and dashboard formatting without a UNO Q attached, but sends nothing
    anywhere.

SPDX-License-Identifier: BSD-3-Clause
"""

from __future__ import annotations

import signal
import sys
import threading
import time

from drowsyguard.link import icd
from drowsyguard.link.ap_rt_transport import ApRtTransport, NullTransport

DMS_STATUS_PERIOD_S = 0.1  # CAN spec 04: DMS_STATUS cadence, 100 ms
SUMMARY_PERIOD_S = 1.0

# vcs-mcxn947 has no real speed sensor (see its README "What is NOT wired
# yet" -- motor current/voltage isn't wired either) -- VCS_STATUS carries
# motor PWM duty_left_pct/duty_right_pct, not km/h. MAX_SPEED_KMH is a
# bench/demo constant mapping duty% to a "simulated vehicle speed" figure
# for this dashboard, nothing the VCS firmware itself claims to measure.
MAX_SPEED_KMH = 40.0


class KeyboardAlertSource:
    """Reads single keypresses off stdin on a background thread so the main
    loop's CAN TX timing is never blocked on a blocking input() call.
    0-3 set the alert level sent in the next DMS_STATUS, `c` toggles
    flag_calib_done (VCS needs this set before DISARMED -> ARMED_IDLE,
    spec 05 VEH-011), `q` requests shutdown."""

    def __init__(self) -> None:
        self._level = icd.AlertLevel.L0_NORMAL
        self._calib_done = False
        self._lock = threading.Lock()
        self._quit_requested = False
        self._thread = threading.Thread(target=self._read_loop, daemon=True)
        self._thread.start()
        print("[control] keys: 0-3 set alert level, c toggle calib_done, q quit")

    def _read_loop(self) -> None:
        for line in sys.stdin:
            key = line.strip().lower()
            if key in ("0", "1", "2", "3"):
                with self._lock:
                    self._level = icd.AlertLevel(int(key))
                print(f"[control] alert_level -> L{key}")
            elif key == "c":
                with self._lock:
                    self._calib_done = not self._calib_done
                print(f"[control] calib_done -> {self._calib_done}")
            elif key == "q":
                self._quit_requested = True
                break

    @property
    def level(self) -> icd.AlertLevel:
        with self._lock:
            return self._level

    @property
    def calib_done(self) -> bool:
        with self._lock:
            return self._calib_done

    @property
    def quit_requested(self) -> bool:
        return self._quit_requested


class ControlLoop:
    """One tick = maybe-send-DMS_STATUS + drain-VCS_STATUS + maybe-print.
    Self-paces on `time_source()` rather than sleeping, so it behaves the
    same whether the caller drives it from a tight while-loop (bench mode)
    or from App.run(user_loop=loop) at an unconfirmed call frequency (App
    Lab mode, see run_app_lab() below) -- calling tick() too often just
    returns early, it never sends faster than DMS_STATUS_PERIOD_S."""

    def __init__(
        self,
        transport: ApRtTransport,
        alert_source: KeyboardAlertSource,
        time_source: "callable[[], float]",
    ) -> None:
        self.transport = transport
        self.alert_source = alert_source
        self.time_source = time_source

        self.seq4 = 0
        self.tx_count = 0
        self.last_send_s = 0.0
        self.last_summary_s = time_source()
        self.last_vcs_status: icd.VcsStatus | None = None

    def tick(self) -> None:
        now = self.time_source()

        if now - self.last_send_s >= DMS_STATUS_PERIOD_S:
            self.last_send_s = now
            self._send_dms_status()

        vcs = self.transport.poll_vcs_status()
        if vcs is not None:
            self.last_vcs_status = vcs

        for event_id, event_seq in self.transport.poll_events():
            print(f"[rx] VCS_EVENT id={event_id} seq={event_seq}")

        if now - self.last_summary_s >= SUMMARY_PERIOD_S:
            self.last_summary_s = now
            self._print_summary()

    def _send_dms_status(self) -> None:
        status = icd.DmsStatus(
            alert_level=self.alert_source.level,
            seq=self.seq4,
            d1_state=icd.DomainWireState.IDLE,
            d2_state=icd.DomainWireState.IDLE,
            d3_state=icd.DomainWireState.IDLE,
            d3_avail=icd.D3Availability.AVAILABLE,
            perclos_pct=255,  # 255 = invalid, no real D3 sensor in this test loop
            eye_closure_ms=65535,  # 65535 = not measurable
            face_conf_pct=100,  # not 255 ("no face"), so nothing else reads this as invalid
            flag_calib_done=self.alert_source.calib_done,
        )
        self.transport.send_dms_status(status)
        self.seq4 = (self.seq4 + 1) % 16
        self.tx_count += 1

    def _print_summary(self) -> None:
        vcs = self.last_vcs_status
        if vcs is None:
            print(f"[dashboard] tx_dms_status={self.tx_count} -- no VCS_STATUS received yet")
            return

        avg_duty_pct = (vcs.duty_left_pct + vcs.duty_right_pct) / 2.0
        simulated_speed_kmh = avg_duty_pct * MAX_SPEED_KMH / 100.0
        print(
            f"[dashboard] tx_dms_status={self.tx_count} "
            f"vehicle_state={vcs.vehicle_state.name} "
            f"speed_cap={vcs.speed_cap_pct}% "
            f"simulated_speed={simulated_speed_kmh:.1f} km/h "
            f"(dutyL={vcs.duty_left_pct}% dutyR={vcs.duty_right_pct}%) "
            f"estop={vcs.estop_active} can_timeout={vcs.fault_can_timeout}"
        )

    def send_shutdown_status(self) -> None:
        """DEV-045-style safety net: last frame this process gets to send is
        L0 + calib_done=False, same reasoning as the real main.py's
        `_Pipeline.shutdown()`. Not a promise the VCS keeps listening --
        its own CAN-063 timeout supervisor takes over regardless."""
        status = icd.DmsStatus(
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
        self.transport.send_dms_status(status)


def run_bench_mode() -> None:
    """No arduino.app_utils here -- runs the keyboard/dashboard loop against
    NullTransport so you can sanity check it before touching real hardware.
    Sends/receives nothing over any real link."""
    print("[main] bench mode -- arduino.app_utils not importable, using NullTransport "
          "(nothing is actually sent anywhere)")
    alert_source = KeyboardAlertSource()
    control = ControlLoop(NullTransport(), alert_source, time.monotonic)

    try:
        while not alert_source.quit_requested:
            control.tick()
            time.sleep(0.01)
    finally:
        control.send_shutdown_status()


def run_app_lab() -> None:
    """App Lab entry point -- only called when arduino.app_utils actually
    imports, i.e. only on the real UNO Q. Same App.run(user_loop=loop)
    pattern as the canonical main.py; see that file's run_app_lab()
    docstring for exactly what is confirmed vs. assumed about this pattern
    and about signal handling under App.run()."""
    from arduino.app_utils import App  # type: ignore[import-not-found]

    from drowsyguard.link.ap_rt_transport import RouterBridgeTransport

    print("[main] App Lab mode -- RouterBridgeTransport -> sketch.ino -> FDCAN1")
    alert_source = KeyboardAlertSource()
    transport = RouterBridgeTransport()
    control = ControlLoop(transport, alert_source, time.monotonic)

    shutdown_requested = False

    def _handle_signal(signum: int, frame: object) -> None:
        nonlocal shutdown_requested
        shutdown_requested = True
        control.send_shutdown_status()  # best-effort, see docstring above

    signal.signal(signal.SIGTERM, _handle_signal)
    signal.signal(signal.SIGINT, _handle_signal)

    def loop() -> None:
        if not shutdown_requested and not alert_source.quit_requested:
            control.tick()

    App.run(user_loop=loop)


def main() -> None:
    try:
        import arduino.app_utils  # noqa: F401

        run_app_lab()
    except ImportError:
        run_bench_mode()


if __name__ == "__main__":
    main()
