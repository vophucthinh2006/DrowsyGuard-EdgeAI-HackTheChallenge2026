"""
Minimal, dependency-free MJPEG streaming server.

Why this exists: Arduino App Lab Python apps normally run inside a headless
Docker container on the board's Linux side (no X server, no physical
display attached to that process) - so cv2.imshow() will simply fail or do
nothing useful there. The standard way these apps show video is through a
small web server that the board serves, which you open in a browser (this
is exactly what Arduino's own example apps do, e.g. "Video Object
Detection" + "WebUI - HTML" bricks, viewed at http://<board-ip>:7000).

This module implements that pattern from scratch using only Python's
built-in http.server, so no extra pip dependency (like Flask) is required.
Call `WebStreamer(port=7000).start()` once, then call
`streamer.update_frame(bgr_frame)` every loop iteration - any browser
pointed at http://<board-ip>:7000/ will show the live annotated video.
"""
import io
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import cv2

_BOUNDARY = "frame"

_INDEX_HTML = """<!DOCTYPE html>
<html>
<head><title>Driver Monitoring - Live View</title></head>
<body style="background:#111;margin:0;display:flex;align-items:center;
justify-content:center;height:100vh;">
  <img src="/stream.mjpg" style="max-width:100%;max-height:100%;" />
</body>
</html>
"""


class WebStreamer:
    def __init__(self, port=7000):
        self.port = port
        self._lock = threading.Lock()
        self._latest_jpeg = None
        self._server = None
        self._thread = None

    def update_frame(self, frame_bgr):
        """Encode and store the latest frame (call this once per loop iteration)."""
        ok, buf = cv2.imencode(".jpg", frame_bgr, [int(cv2.IMWRITE_JPEG_QUALITY), 80])
        if not ok:
            return
        with self._lock:
            self._latest_jpeg = buf.tobytes()

    def _get_latest_jpeg(self):
        with self._lock:
            return self._latest_jpeg

    def start(self):
        streamer = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, fmt, *args):
                pass  # silence per-request logging

            def do_GET(self):
                if self.path in ("/", "/index.html"):
                    body = _INDEX_HTML.encode("utf-8")
                    self.send_response(200)
                    self.send_header("Content-Type", "text/html")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                elif self.path == "/stream.mjpg":
                    self.send_response(200)
                    self.send_header(
                        "Content-Type", f"multipart/x-mixed-replace; boundary={_BOUNDARY}"
                    )
                    self.end_headers()
                    try:
                        while True:
                            jpeg = streamer._get_latest_jpeg()
                            if jpeg is None:
                                time.sleep(0.05)
                                continue
                            self.wfile.write(f"--{_BOUNDARY}\r\n".encode())
                            self.wfile.write(b"Content-Type: image/jpeg\r\n")
                            self.wfile.write(f"Content-Length: {len(jpeg)}\r\n\r\n".encode())
                            self.wfile.write(jpeg)
                            self.wfile.write(b"\r\n")
                            time.sleep(0.03)
                    except (BrokenPipeError, ConnectionResetError):
                        pass
                else:
                    self.send_response(404)
                    self.end_headers()

        self._server = ThreadingHTTPServer(("0.0.0.0", self.port), Handler)
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)
        self._thread.start()
        print(f"[webui] Live view available at http://<board-ip>:{self.port}/")

    def stop(self):
        if self._server is not None:
            self._server.shutdown()
