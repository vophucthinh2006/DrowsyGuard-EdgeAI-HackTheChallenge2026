#!/usr/bin/env python3
"""
Test client to stream images/video over WebSocket to the Arduino Q WebSocketCamera server.
Supports local webcam streaming or static image file streaming.

Requirements:
    pip install opencv-python websocket-client

Usage:
    python tools/test_websocket_camera.py --ip localhost --port 8080
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

# Add terminal coloring helpers
class Colors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

# Check dependencies
try:
    import cv2
except ImportError:
    print(f"{Colors.FAIL}Error: 'opencv-python' is not installed.{Colors.ENDC}")
    print("Please install it by running:")
    print(f"  {Colors.BOLD}pip install opencv-python{Colors.ENDC}")
    sys.exit(1)

try:
    import websocket
except ImportError:
    print(f"{Colors.FAIL}Error: 'websocket-client' is not installed.{Colors.ENDC}")
    print("Please install it by running:")
    print(f"  {Colors.BOLD}pip install websocket-client{Colors.ENDC}")
    sys.exit(1)


def parse_resolution(res_str: str) -> tuple[int, int]:
    try:
        w, h = map(int, res_str.lower().split('x'))
        return w, h
    except Exception:
        print(f"{Colors.WARNING}Invalid resolution format '{res_str}'. Defaulting to 640x480.{Colors.ENDC}")
        return 640, 480


def main() -> None:
    parser = argparse.ArgumentParser(description="Arduino Q WebSocket Camera Test Streamer")
    parser.add_argument("--ip", type=str, default="127.0.0.1", help="IP address of the Arduino Q board")
    parser.add_argument("--port", type=int, default=8080, help="Port of the WebSocketCamera server")
    parser.add_argument("--secret", type=str, default=None, help="Secret/OTP passcode if server is authenticated")
    parser.add_argument("--source", type=str, default="0", help="Webcam index (e.g., 0) or path to an image/video file")
    parser.add_argument("--fps", type=int, default=15, help="Frames per second to stream")
    parser.add_argument("--resolution", type=str, default="640x480", help="Resolution to resize frames (e.g., 640x480, 1280x720)")
    parser.add_argument("--quality", type=int, default=80, help="JPEG compression quality (1-100)")
    parser.add_argument("--no-preview", action="store_true", help="Disable local OpenCV preview window")
    args = parser.parse_args()

    width, height = parse_resolution(args.resolution)
    fps_delay = 1.0 / args.fps

    # Construct the WebSocket URL
    ws_url = f"ws://{args.ip}:{args.port}?raw=true"
    if args.secret:
        ws_url += f"&secret={args.secret}"

    print(f"{Colors.HEADER}{Colors.BOLD}=== WebSocket Camera Test Client ==={Colors.ENDC}")
    print(f"Target URL:  {Colors.OKCYAN}{ws_url}{Colors.ENDC}")
    print(f"Source:      {Colors.OKCYAN}{args.source}{Colors.ENDC}")
    print(f"Resolution:  {Colors.OKCYAN}{width}x{height}{Colors.ENDC}")
    print(f"Target FPS:  {Colors.OKCYAN}{args.fps}{Colors.ENDC}")
    print(f"JPEG Quality:{Colors.OKCYAN}{args.quality}%{Colors.ENDC}")
    print("====================================")

    # Initialize video/image capture
    is_webcam = False
    cap = None
    static_img = None

    if args.source.isdigit():
        is_webcam = True
        camera_idx = int(args.source)
        print(f"Opening webcam index {camera_idx}...")
        cap = cv2.VideoCapture(camera_idx)
        if not cap.isOpened():
            print(f"{Colors.FAIL}Error: Could not open webcam index {camera_idx}.{Colors.ENDC}")
            sys.exit(1)
        # Set hardware properties if supported
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
    else:
        src_path = Path(args.source)
        if not src_path.exists():
            print(f"{Colors.FAIL}Error: Source file '{args.source}' does not exist.{Colors.ENDC}")
            sys.exit(1)
        
        # Check if it is a video or image
        suffix = src_path.suffix.lower()
        if suffix in ['.mp4', '.avi', '.mov', '.mkv']:
            print(f"Opening video file '{args.source}'...")
            cap = cv2.VideoCapture(args.source)
            if not cap.isOpened():
                print(f"{Colors.FAIL}Error: Could not open video file '{args.source}'.{Colors.ENDC}")
                sys.exit(1)
        else:
            print(f"Loading static image '{args.source}'...")
            static_img = cv2.imread(str(src_path))
            if static_img is None:
                print(f"{Colors.FAIL}Error: Could not load image '{args.source}'.{Colors.ENDC}")
                sys.exit(1)
            static_img = cv2.resize(static_img, (width, height))

    # Connect to WebSocket server
    ws = None
    try:
        print("Connecting to WebSocket server...")
        ws = websocket.WebSocket()
        ws.connect(ws_url)
        print(f"{Colors.OKGREEN}{Colors.BOLD}Connected successfully!{Colors.ENDC} Press Ctrl+C or 'q' to stop.")
    except Exception as e:
        print(f"{Colors.FAIL}Connection failed: {e}{Colors.ENDC}")
        print("Please ensure the Arduino board application is running and reachable.")
        if cap:
            cap.release()
        sys.exit(1)

    frame_count = 0
    start_time = time.time()
    last_fps_time = time.time()

    try:
        while True:
            loop_start = time.time()
            
            # Obtain frame
            if is_webcam or cap is not None:
                success, frame = cap.read()
                if not success:
                    # Loop video if reached end
                    if cap.get(cv2.CAP_PROP_POSITION) == cap.get(cv2.CAP_PROP_FRAME_COUNT):
                        cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
                        success, frame = cap.read()
                    
                    if not success:
                        print(f"\n{Colors.WARNING}End of video feed or frame capture failed.{Colors.ENDC}")
                        break
                frame = cv2.resize(frame, (width, height))
            else:
                # Static image
                frame = static_img.copy()

            # Encode frame as JPEG
            encode_param = [int(cv2.IMWRITE_JPEG_QUALITY), args.quality]
            success, encoded_img = cv2.imencode('.jpg', frame, encode_param)
            if not success:
                print(f"{Colors.WARNING}Failed to encode image to JPEG.{Colors.ENDC}")
                continue

            # Convert to bytes
            jpeg_bytes = encoded_img.tobytes()

            # Send bytes as BINARY WebSocket frame
            try:
                ws.send(jpeg_bytes, opcode=websocket.ABNF.OPCODE_BINARY)
                frame_count += 1
            except (websocket.WebSocketConnectionClosedException, ConnectionResetError) as e:
                print(f"\n{Colors.FAIL}Connection closed by server: {e}{Colors.ENDC}")
                break
            except Exception as e:
                print(f"\n{Colors.FAIL}Error sending frame: {e}{Colors.ENDC}")
                break

            # Local preview
            if not args.no_preview:
                # Add overlay info
                preview_frame = frame.copy()
                cv2.putText(preview_frame, f"Sent: {frame_count}", (10, 30),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
                cv2.imshow("WebSocket Camera Test Client (Press 'q' to exit)", preview_frame)
                if cv2.waitKey(1) & 0xFF == ord('q'):
                    print("\nExit requested by user.")
                    break

            # Print status update once a second
            now = time.time()
            if now - last_fps_time >= 1.0:
                elapsed = now - last_fps_time
                current_fps = frame_count / (now - start_time)
                print(f"\rStreaming: {frame_count} frames sent | Avg FPS: {current_fps:.2f}  ", end="", flush=True)
                last_fps_time = now

            # Control framerate
            elapsed_loop = time.time() - loop_start
            sleep_duration = max(0.0, fps_delay - elapsed_loop)
            if sleep_duration > 0:
                time.sleep(sleep_duration)

    except KeyboardInterrupt:
        print("\nStreaming stopped by user.")

    finally:
        print("\nCleaning up...")
        if ws:
            try:
                ws.close()
            except Exception:
                pass
        if cap:
            cap.release()
        if not args.no_preview:
            cv2.destroyAllWindows()
        
        total_elapsed = time.time() - start_time
        avg_fps = frame_count / total_elapsed if total_elapsed > 0 else 0
        print(f"{Colors.OKGREEN}Finished. Sent {frame_count} frames in {total_elapsed:.2f} seconds (Avg FPS: {avg_fps:.2f}).{Colors.ENDC}")
