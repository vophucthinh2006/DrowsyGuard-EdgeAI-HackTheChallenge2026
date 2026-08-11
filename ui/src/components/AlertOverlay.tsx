import React, { useEffect } from 'react';
import { DriverStatusPacket } from '../types/websocket';
import { AlertTriangle, AlertOctagon, Bell, Zap, Wind } from 'lucide-react';

interface AlertOverlayProps {
  status: DriverStatusPacket | null;
}

export const AlertOverlay: React.FC<AlertOverlayProps> = ({ status }) => {
  const alertLevel = status?.alert_level ?? 0;

  // Sound effect handler using Web Audio API synthesis
  useEffect(() => {
    if (alertLevel === 0) return;

    try {
      const audioCtx = new (window.AudioContext || (window as any).webkitAudioContext)();
      const osc = audioCtx.createOscillator();
      const gain = audioCtx.createGain();

      osc.type = alertLevel === 3 ? 'sawtooth' : 'sine';
      osc.frequency.value = alertLevel === 3 ? 3200 : alertLevel === 2 ? 2800 : 2000;

      gain.gain.setValueAtTime(0.1, audioCtx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + 0.3);

      osc.connect(gain);
      gain.connect(audioCtx.destination);

      osc.start();
      osc.stop(audioCtx.currentTime + 0.3);
    } catch (e) {
      // Audio playback blocked or uninitialized
    }
  }, [alertLevel, status?.seq]);

  if (alertLevel === 0) return null;

  return (
    <div className="alert-overlay-backdrop">
      {alertLevel === 1 && (
        <div className="alert-card-popup level-1">
          <Bell size={28} />
          <div>
            <div style={{ fontWeight: 800, fontSize: '1.1rem' }}>LEVEL 1: EARLY WARNING</div>
            <div style={{ fontSize: '0.85rem', opacity: 0.9 }}>Early fatigue detected. Maintain focus on the road.</div>
          </div>
        </div>
      )}

      {alertLevel === 2 && (
        <div className="alert-card-popup level-2">
          <AlertTriangle size={32} />
          <div>
            <div style={{ fontWeight: 900, fontSize: '1.2rem', letterSpacing: '0.5px' }}>
              LEVEL 2: DROWSINESS ALARM DETECTED!
            </div>
            <div style={{ fontSize: '0.9rem', opacity: 0.9, marginTop: '2px' }}>
              Microsleep or severe fatigue. Speed capped at 50%. Actuators triggered.
            </div>
            <div style={{ display: 'flex', gap: '12px', marginTop: '8px', fontSize: '0.8rem', fontWeight: 700 }}>
              <span style={{ display: 'flex', alignItems: 'center', gap: '4px' }}><Bell size={14} /> Buzzer 2.8kHz</span>
              <span style={{ display: 'flex', alignItems: 'center', gap: '4px' }}><Zap size={14} /> Seat Haptic</span>
              <span style={{ display: 'flex', alignItems: 'center', gap: '4px' }}><Wind size={14} /> Fan Relay</span>
            </div>
          </div>
        </div>
      )}

      {alertLevel === 3 && (
        <div className="alert-card-popup level-3">
          <AlertOctagon size={40} style={{ animation: 'pulse-glow 0.5s infinite alternate' }} />
          <div>
            <div style={{ fontWeight: 900, fontSize: '1.4rem', letterSpacing: '1px' }}>
              LEVEL 3: CRITICAL DANGER — SAFE STOP IN PROGRESS!
            </div>
            <div style={{ fontSize: '0.95rem', opacity: 0.95, marginTop: '4px' }}>
              Driver unresponsive (≥ 3s closure). Vehicle executing controlled deceleration to 0 km/h.
            </div>
          </div>
        </div>
      )}
    </div>
  );
};
