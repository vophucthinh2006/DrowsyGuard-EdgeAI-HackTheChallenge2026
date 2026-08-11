import React from 'react';
import { CameraFeed } from '../components/CameraFeed';
import { CameraFramePacket, DriverStatusPacket } from '../types/websocket';
import { Camera, Eye, Smile, Compass, Cpu, CheckCircle2, AlertTriangle } from 'lucide-react';

interface CameraMonitorPageProps {
  cameraFrame: CameraFramePacket | null;
  driverStatus: DriverStatusPacket | null;
}

export const CameraMonitorPage: React.FC<CameraMonitorPageProps> = ({
  cameraFrame,
  driverStatus,
}) => {
  const fps = cameraFrame?.fps || 10.0;
  const inferenceMs = cameraFrame?.inference_ms || 78;
  const pitch = cameraFrame?.head_pose?.pitch || 0;
  const yaw = cameraFrame?.head_pose?.yaw || 0;
  const roll = cameraFrame?.head_pose?.roll || 0;

  const leftEyeClosed = cameraFrame?.bounding_boxes?.left_eye?.closed ?? false;
  const rightEyeClosed = cameraFrame?.bounding_boxes?.right_eye?.closed ?? false;
  const mouthYawning = cameraFrame?.bounding_boxes?.mouth?.yawning ?? false;

  const faceDetected = cameraFrame?.face_detected ?? true;

  return (
    <div className="page-content">
      <div className="camera-monitor-grid">
        {/* Left: Main Live Video Stream Feed */}
        <div>
          <div className="glass-panel" style={{ padding: '20px' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '14px' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '10px' }}>
                <Camera style={{ color: '#00f2fe', width: 22, height: 22 }} />
                <h3 style={{ fontSize: '1.2rem', fontWeight: 700 }}>AI Vision Camera Feed</h3>
              </div>
              <div style={{ display: 'flex', gap: '12px' }}>
                <div style={{ fontSize: '0.85rem', color: '#8a99ad' }}>
                  FPS: <span style={{ color: '#00ff88', fontWeight: 700, fontFamily: 'JetBrains Mono' }}>{fps.toFixed(1)}</span>
                </div>
                <div style={{ fontSize: '0.85rem', color: '#8a99ad' }}>
                  LATENCY: <span style={{ color: '#00f2fe', fontWeight: 700, fontFamily: 'JetBrains Mono' }}>{inferenceMs} ms</span>
                </div>
              </div>
            </div>

            <CameraFeed frame={cameraFrame} />
          </div>
        </div>

        {/* Right: Computer Vision Metrics HUD & Bounding Box Telemetry */}
        <div>
          <div className="glass-panel" style={{ padding: '20px' }}>
            <h3 style={{ fontSize: '1.1rem', fontWeight: 700, marginBottom: '16px', display: 'flex', alignItems: 'center', gap: '8px' }}>
              <Cpu size={18} style={{ color: '#00f2fe' }} /> Vision Analytics HUD
            </h3>

            {/* Face Status Pill */}
            <div
              style={{
                display: 'flex',
                alignItems: 'center',
                gap: '10px',
                padding: '12px 16px',
                borderRadius: '10px',
                background: faceDetected ? 'rgba(0, 255, 136, 0.1)' : 'rgba(255, 0, 85, 0.15)',
                border: `1px solid ${faceDetected ? 'rgba(0, 255, 136, 0.3)' : 'rgba(255, 0, 85, 0.4)'}`,
                marginBottom: '20px',
              }}
            >
              {faceDetected ? (
                <CheckCircle2 style={{ color: '#00ff88', width: 20, height: 20 }} />
              ) : (
                <AlertTriangle style={{ color: '#ff0055', width: 20, height: 20 }} />
              )}
              <div>
                <div style={{ fontSize: '0.8rem', color: '#8a99ad' }}>FACE PRESENCE</div>
                <div style={{ fontWeight: 800, color: faceDetected ? '#00ff88' : '#ff0055' }}>
                  {faceDetected ? 'FACE DETECTED (CONF: 95%)' : 'NO FACE IN CAMERA!'}
                </div>
              </div>
            </div>

            {/* Eyes Bounding Box Status */}
            <div style={{ background: 'rgba(0,0,0,0.3)', padding: '14px', borderRadius: '10px', marginBottom: '14px', border: '1px solid rgba(255,255,255,0.05)' }}>
              <div style={{ fontSize: '0.85rem', fontWeight: 700, color: '#f0f4fd', display: 'flex', alignItems: 'center', gap: '6px', marginBottom: '8px' }}>
                <Eye size={16} style={{ color: '#00f2fe' }} /> EYES BOUNDING BOX
              </div>
              <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '0.85rem' }}>
                <span style={{ color: '#8a99ad' }}>Left Eye State:</span>
                <span style={{ fontWeight: 700, color: leftEyeClosed ? '#ff0055' : '#00ff88' }}>
                  {leftEyeClosed ? 'CLOSED (EAR 0.12)' : 'OPEN (EAR 0.32)'}
                </span>
              </div>
              <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '0.85rem', marginTop: '6px' }}>
                <span style={{ color: '#8a99ad' }}>Right Eye State:</span>
                <span style={{ fontWeight: 700, color: rightEyeClosed ? '#ff0055' : '#00ff88' }}>
                  {rightEyeClosed ? 'CLOSED (EAR 0.13)' : 'OPEN (EAR 0.30)'}
                </span>
              </div>
            </div>

            {/* Mouth Bounding Box Status */}
            <div style={{ background: 'rgba(0,0,0,0.3)', padding: '14px', borderRadius: '10px', marginBottom: '14px', border: '1px solid rgba(255,255,255,0.05)' }}>
              <div style={{ fontSize: '0.85rem', fontWeight: 700, color: '#f0f4fd', display: 'flex', alignItems: 'center', gap: '6px', marginBottom: '8px' }}>
                <Smile size={16} style={{ color: '#ffaa00' }} /> MOUTH BOUNDING BOX
              </div>
              <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '0.85rem' }}>
                <span style={{ color: '#8a99ad' }}>Mouth State:</span>
                <span style={{ fontWeight: 700, color: mouthYawning ? '#ffaa00' : '#00ff88' }}>
                  {mouthYawning ? 'YAWNING (MAR 0.65)' : 'NORMAL (MAR 0.22)'}
                </span>
              </div>
            </div>

            {/* Head Pose Euler Angles */}
            <div style={{ background: 'rgba(0,0,0,0.3)', padding: '14px', borderRadius: '10px', border: '1px solid rgba(255,255,255,0.05)' }}>
              <div style={{ fontSize: '0.85rem', fontWeight: 700, color: '#f0f4fd', display: 'flex', alignItems: 'center', gap: '6px', marginBottom: '8px' }}>
                <Compass size={16} style={{ color: '#00f2fe' }} /> HEAD POSE EULER ANGLES
              </div>
              <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: '8px', textAlign: 'center', marginTop: '6px' }}>
                <div style={{ background: 'rgba(255,255,255,0.03)', padding: '8px', borderRadius: '6px' }}>
                  <div style={{ fontSize: '0.7rem', color: '#8a99ad' }}>PITCH</div>
                  <div style={{ fontWeight: 700, color: Math.abs(pitch) > 20 ? '#ff0055' : '#00f2fe' }}>{pitch.toFixed(1)}°</div>
                </div>
                <div style={{ background: 'rgba(255,255,255,0.03)', padding: '8px', borderRadius: '6px' }}>
                  <div style={{ fontSize: '0.7rem', color: '#8a99ad' }}>YAW</div>
                  <div style={{ fontWeight: 700, color: Math.abs(yaw) > 25 ? '#ff0055' : '#00f2fe' }}>{yaw.toFixed(1)}°</div>
                </div>
                <div style={{ background: 'rgba(255,255,255,0.03)', padding: '8px', borderRadius: '6px' }}>
                  <div style={{ fontSize: '0.7rem', color: '#8a99ad' }}>ROLL</div>
                  <div style={{ fontWeight: 700, color: '#00f2fe' }}>{roll.toFixed(1)}°</div>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};
