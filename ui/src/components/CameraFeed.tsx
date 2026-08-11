import React, { useEffect, useRef } from 'react';
import { CameraFramePacket } from '../types/websocket';
import { UserX, AlertOctagon, Eye, Smile, Activity } from 'lucide-react';

interface CameraFeedProps {
  frame: CameraFramePacket | null;
}

export const CameraFeed: React.FC<CameraFeedProps> = ({ frame }) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const imgRef = useRef<HTMLImageElement | null>(null);

  const faceDetected = frame?.face_detected ?? true;

  useEffect(() => {
    if (!frame || !canvasRef.current) return;
    const canvas = canvasRef.current;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const imgWidth = frame.image_width || 640;
    const imgHeight = frame.image_height || 480;

    canvas.width = imgWidth;
    canvas.height = imgHeight;

    ctx.clearRect(0, 0, imgWidth, imgHeight);

    // Draw background image if available, else dark mock background
    if (frame.frame_jpeg) {
      const img = new Image();
      img.onload = () => {
        ctx.drawImage(img, 0, 0, imgWidth, imgHeight);
        drawOverlays(ctx, frame, imgWidth, imgHeight);
      };
      img.src = frame.frame_jpeg;
    } else {
      // Dark mock video frame
      ctx.fillStyle = '#0f141f';
      ctx.fillRect(0, 0, imgWidth, imgHeight);
      drawGridLines(ctx, imgWidth, imgHeight);
      drawOverlays(ctx, frame, imgWidth, imgHeight);
    }
  }, [frame]);

  const drawGridLines = (ctx: CanvasRenderingContext2D, w: number, h: number) => {
    ctx.strokeStyle = 'rgba(0, 242, 254, 0.05)';
    ctx.lineWidth = 1;
    for (let x = 0; x < w; x += 40) {
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke();
    }
    for (let y = 0; y < h; y += 40) {
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
    }
  };

  const drawOverlays = (ctx: CanvasRenderingContext2D, data: CameraFramePacket, w: number, h: number) => {
    if (!data.face_detected) return;

    const boxes = data.bounding_boxes || {};

    // 1. Full Face Box
    if (boxes.face) {
      const { x, y, width, height } = boxes.face;
      ctx.strokeStyle = '#00f2fe';
      ctx.lineWidth = 2;
      ctx.setLineDash([6, 6]);
      ctx.strokeRect(x, y, width, height);
      ctx.setLineDash([]); // reset

      // Label
      ctx.fillStyle = '#00f2fe';
      ctx.font = '700 12px "Outfit", sans-serif';
      ctx.fillText(`FACE CONF: ${Math.round((data.face_confidence || 0.95) * 100)}%`, x, y - 6);
    }

    // 2. Left Eye Box
    if (boxes.left_eye) {
      const { x, y, width, height, closed } = boxes.left_eye;
      const color = closed ? '#ff0055' : '#00ff88';
      ctx.strokeStyle = color;
      ctx.lineWidth = 2;
      ctx.strokeRect(x, y, width, height);

      ctx.fillStyle = color;
      ctx.font = '600 11px "Outfit", sans-serif';
      ctx.fillText(`L-EYE (${closed ? 'CLOSED' : 'OPEN'})`, x, y - 4);
    }

    // 3. Right Eye Box
    if (boxes.right_eye) {
      const { x, y, width, height, closed } = boxes.right_eye;
      const color = closed ? '#ff0055' : '#00ff88';
      ctx.strokeStyle = color;
      ctx.lineWidth = 2;
      ctx.strokeRect(x, y, width, height);

      ctx.fillStyle = color;
      ctx.font = '600 11px "Outfit", sans-serif';
      ctx.fillText(`R-EYE (${closed ? 'CLOSED' : 'OPEN'})`, x, y - 4);
    }

    // 4. Mouth Box
    if (boxes.mouth) {
      const { x, y, width, height, yawning } = boxes.mouth;
      const color = yawning ? '#ffaa00' : '#00f2fe';
      ctx.strokeStyle = color;
      ctx.lineWidth = 2;
      ctx.strokeRect(x, y, width, height);

      ctx.fillStyle = color;
      ctx.font = '600 11px "Outfit", sans-serif';
      ctx.fillText(`MOUTH (${yawning ? 'YAWNING!' : 'NORMAL'})`, x, y - 4);
    }
  };

  return (
    <div className="canvas-wrapper">
      <canvas ref={canvasRef} className="canvas-feed" />

      {/* Warning Banner Overlay when No Face Detected */}
      {!faceDetected && (
        <div className="no-face-overlay">
          <UserX size={64} style={{ color: '#ff0055', animation: 'pulse-glow 1s infinite' }} />
          <div className="no-face-title">CẢNH BÁO: KHÔNG CÓ KHUÔN MẶT</div>
          <div style={{ color: 'rgba(255, 255, 255, 0.8)', fontSize: '1rem', fontWeight: 600 }}>
            NO FACE DETECTED IN CAMERA FEED
          </div>
          <div style={{ fontSize: '0.85rem', color: '#ffaa00', marginTop: '6px' }}>
            Sensor Lost Timeout Active (DMS Fault State)
          </div>
        </div>
      )}
    </div>
  );
};
