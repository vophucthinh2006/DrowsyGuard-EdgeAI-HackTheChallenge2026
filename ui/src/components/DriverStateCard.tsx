import React from 'react';
import { DriverStatusPacket } from '../types/websocket';
import { Shield, Eye, AlertTriangle, Moon, Smile } from 'lucide-react';

interface DriverStateCardProps {
  status: DriverStatusPacket | null;
}

export const DriverStateCard: React.FC<DriverStateCardProps> = ({ status }) => {
  if (!status) {
    return (
      <div className="glass-panel" style={{ padding: '24px', textAlign: 'center', color: '#8a99ad' }}>
        Waiting for driver status stream...
      </div>
    );
  }

  const getAlertColor = (level: number) => {
    switch (level) {
      case 0: return '#00ff88';
      case 1: return '#ffaa00';
      case 2: return '#ff0055';
      case 3: return '#ff0000';
      default: return '#8a99ad';
    }
  };

  const alertColor = getAlertColor(status.alert_level);

  // Compute progress percentages
  const eyeClosurePct = Math.min(100, (status.eye_closure_ms / (status.eye_closure_threshold_critical || 3000)) * 100);
  const perclosPct = Math.min(100, (status.perclos_pct / (status.perclos_threshold_severe || 15)) * 100);
  const yawnPct = Math.min(100, (status.yawn_count / (status.yawn_threshold_severe || 3)) * 100);
  const eorPct = Math.min(100, (status.eor_cum_ms / (status.eor_threshold_severe || 6000)) * 100);

  return (
    <div className="glass-panel" style={{ padding: '24px' }}>
      {/* Header Badge */}
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '20px' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '10px' }}>
          <Shield style={{ color: alertColor, width: 24, height: 24 }} />
          <h3 style={{ fontSize: '1.2rem', fontWeight: 700 }}>Driver Risk Assessment</h3>
        </div>
        <div
          style={{
            background: `${alertColor}20`,
            color: alertColor,
            border: `1px solid ${alertColor}60`,
            padding: '6px 16px',
            borderRadius: '20px',
            fontWeight: 800,
            fontSize: '0.9rem',
            letterSpacing: '0.5px',
            boxShadow: `0 0 16px ${alertColor}30`,
          }}
        >
          {status.alert_name || `LEVEL ${status.alert_level}`}
        </div>
      </div>

      {/* Domain Status Pills */}
      <div style={{ display: 'flex', gap: '10px', marginBottom: '24px' }}>
        <div style={{ flex: 1, background: 'rgba(255,255,255,0.03)', padding: '10px', borderRadius: '8px', border: '1px solid rgba(255,255,255,0.05)', textAlign: 'center' }}>
          <div style={{ fontSize: '0.75rem', color: '#8a99ad', fontWeight: 600 }}>D1 DISTRACTION</div>
          <div style={{ fontWeight: 800, color: status.d1_state === 'SEVERE' ? '#ff0055' : status.d1_state === 'ACTIVE' ? '#ffaa00' : '#00ff88', marginTop: '4px' }}>
            {status.d1_state}
          </div>
        </div>
        <div style={{ flex: 1, background: 'rgba(255,255,255,0.03)', padding: '10px', borderRadius: '8px', border: '1px solid rgba(255,255,255,0.05)', textAlign: 'center' }}>
          <div style={{ fontSize: '0.75rem', color: '#8a99ad', fontWeight: 600 }}>D2 YAWN</div>
          <div style={{ fontWeight: 800, color: status.d2_state === 'SEVERE' ? '#ff0055' : status.d2_state === 'ACTIVE' ? '#ffaa00' : '#00ff88', marginTop: '4px' }}>
            {status.d2_state}
          </div>
        </div>
        <div style={{ flex: 1, background: 'rgba(255,255,255,0.03)', padding: '10px', borderRadius: '8px', border: '1px solid rgba(255,255,255,0.05)', textAlign: 'center' }}>
          <div style={{ fontSize: '0.75rem', color: '#8a99ad', fontWeight: 600 }}>D3 CLOSURE</div>
          <div style={{ fontWeight: 800, color: status.d3_state === 'CRITICAL' || status.d3_state === 'SEVERE' ? '#ff0055' : status.d3_state === 'ACTIVE' ? '#ffaa00' : '#00ff88', marginTop: '4px' }}>
            {status.d3_state}
          </div>
        </div>
      </div>

      {/* Progress Bars */}

      {/* 1. Eye Closure */}
      <div className="metric-row">
        <div className="metric-header">
          <span className="metric-title" style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
            <Eye size={16} /> Continuous Eye Closure
          </span>
          <span className="metric-value" style={{ color: eyeClosurePct >= 80 ? '#ff0055' : '#00f2fe' }}>
            {status.eye_closure_ms} ms / 3000 ms
          </span>
        </div>
        <div className="progress-track">
          <div
            className="progress-fill"
            style={{
              width: `${eyeClosurePct}%`,
              backgroundColor: eyeClosurePct >= 80 ? '#ff0055' : eyeClosurePct >= 40 ? '#ffaa00' : '#00f2fe',
            }}
          />
        </div>
      </div>

      {/* 2. PERCLOS */}
      <div className="metric-row">
        <div className="metric-header">
          <span className="metric-title" style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
            <Moon size={16} /> PERCLOS Trend (60s window)
          </span>
          <span className="metric-value" style={{ color: perclosPct >= 80 ? '#ff0055' : '#00f2fe' }}>
            {status.perclos_pct}% / 15% SEVERE
          </span>
        </div>
        <div className="progress-track">
          <div
            className="progress-fill"
            style={{
              width: `${perclosPct}%`,
              backgroundColor: perclosPct >= 80 ? '#ff0055' : perclosPct >= 50 ? '#ffaa00' : '#00f2fe',
            }}
          />
        </div>
      </div>

      {/* 3. Yawns */}
      <div className="metric-row">
        <div className="metric-header">
          <span className="metric-title" style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
            <Smile size={16} /> Yawn Count (2 min window)
          </span>
          <span className="metric-value" style={{ color: yawnPct >= 66 ? '#ffaa00' : '#00f2fe' }}>
            {status.yawn_count} / 3 events
          </span>
        </div>
        <div className="progress-track">
          <div
            className="progress-fill"
            style={{
              width: `${yawnPct}%`,
              backgroundColor: yawnPct >= 66 ? '#ff0055' : yawnPct >= 33 ? '#ffaa00' : '#00f2fe',
            }}
          />
        </div>
      </div>

      {/* 4. Distraction EOR */}
      <div className="metric-row">
        <div className="metric-header">
          <span className="metric-title" style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
            <AlertTriangle size={16} /> Distraction EOR (12s window)
          </span>
          <span className="metric-value" style={{ color: eorPct >= 80 ? '#ff0055' : '#00f2fe' }}>
            {status.eor_cum_ms} ms / 6000 ms
          </span>
        </div>
        <div className="progress-track">
          <div
            className="progress-fill"
            style={{
              width: `${eorPct}%`,
              backgroundColor: eorPct >= 80 ? '#ff0055' : eorPct >= 40 ? '#ffaa00' : '#00f2fe',
            }}
          />
        </div>
      </div>
    </div>
  );
};
