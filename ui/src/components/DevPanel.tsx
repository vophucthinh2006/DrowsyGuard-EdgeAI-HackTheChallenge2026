import React, { useState } from 'react';
import { DriverStatusPacket, VehicleStatusPacket } from '../types/websocket';
import { Download, Terminal, Cpu, Zap, Activity } from 'lucide-react';

interface DevPanelProps {
  driverStatus: DriverStatusPacket | null;
  vehicleStatus: VehicleStatusPacket | null;
  latencyMs: number;
  isConnected: boolean;
  onToggleSim: () => void;
}

export const DevPanel: React.FC<DevPanelProps> = ({
  driverStatus,
  vehicleStatus,
  latencyMs,
  isConnected,
  onToggleSim,
}) => {
  const [logHistory, setLogHistory] = useState<string[]>([]);

  // CSV Export Handler
  const handleExportCsv = () => {
    const csvContent =
      'data:text/csv;charset=utf-8,Timestamp,AlertLevel,Speed,PERCLOS,EyeClosureMs,YawnCount\n' +
      `${new Date().toISOString()},${driverStatus?.alert_level || 0},${vehicleStatus?.speed_kmh || 0},${driverStatus?.perclos_pct || 0},${driverStatus?.eye_closure_ms || 0},${driverStatus?.yawn_count || 0}\n`;

    const encodedUri = encodeURI(csvContent);
    const link = document.createElement('a');
    link.setAttribute('href', encodedUri);
    link.setAttribute('download', `drowsyguard_telemetry_${Date.now()}.csv`);
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
  };

  return (
    <div className="glass-panel" style={{ padding: '24px', marginTop: '24px' }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '16px' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '10px' }}>
          <Terminal style={{ color: '#00f2fe', width: 22, height: 22 }} />
          <h3 style={{ fontSize: '1.1rem', fontWeight: 700 }}>Developer Telemetry & Control</h3>
        </div>
        <div style={{ display: 'flex', gap: '10px' }}>
          <button
            onClick={onToggleSim}
            style={{
              background: 'rgba(255, 255, 255, 0.08)',
              border: '1px solid rgba(255, 255, 255, 0.15)',
              color: '#fff',
              padding: '6px 14px',
              borderRadius: '8px',
              cursor: 'pointer',
              fontWeight: 600,
              fontSize: '0.85rem',
            }}
          >
            Simulate Data
          </button>
          <button
            onClick={handleExportCsv}
            style={{
              background: 'rgba(0, 242, 254, 0.15)',
              border: '1px solid rgba(0, 242, 254, 0.4)',
              color: '#00f2fe',
              padding: '6px 14px',
              borderRadius: '8px',
              cursor: 'pointer',
              fontWeight: 600,
              fontSize: '0.85rem',
              display: 'flex',
              alignItems: 'center',
              gap: '6px',
            }}
          >
            <Download size={14} /> Export CSV
          </button>
        </div>
      </div>

      {/* Metrics Row */}
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: '12px' }}>
        <div style={{ background: 'rgba(0,0,0,0.3)', padding: '12px', borderRadius: '8px', border: '1px solid rgba(255,255,255,0.05)' }}>
          <div style={{ fontSize: '0.75rem', color: '#8a99ad', display: 'flex', alignItems: 'center', gap: '4px' }}>
            <Activity size={14} /> WS LATENCY
          </div>
          <div style={{ fontSize: '1.2rem', fontWeight: 700, color: latencyMs < 50 ? '#00ff88' : '#ffaa00', fontFamily: 'JetBrains Mono', marginTop: '4px' }}>
            {latencyMs} ms
          </div>
        </div>

        <div style={{ background: 'rgba(0,0,0,0.3)', padding: '12px', borderRadius: '8px', border: '1px solid rgba(255,255,255,0.05)' }}>
          <div style={{ fontSize: '0.75rem', color: '#8a99ad', display: 'flex', alignItems: 'center', gap: '4px' }}>
            <Zap size={14} /> MOTOR RAIL
          </div>
          <div style={{ fontSize: '1.2rem', fontWeight: 700, color: '#00f2fe', fontFamily: 'JetBrains Mono', marginTop: '4px' }}>
            {vehicleStatus?.battery_voltage_v || 7.4} V
          </div>
        </div>

        <div style={{ background: 'rgba(0,0,0,0.3)', padding: '12px', borderRadius: '8px', border: '1px solid rgba(255,255,255,0.05)' }}>
          <div style={{ fontSize: '0.75rem', color: '#8a99ad', display: 'flex', alignItems: 'center', gap: '4px' }}>
            <Cpu size={14} /> LOGIC SUPPLY
          </div>
          <div style={{ fontSize: '1.2rem', fontWeight: 700, color: '#00ff88', fontFamily: 'JetBrains Mono', marginTop: '4px' }}>
            {vehicleStatus?.logic_supply_v || 5.0} V
          </div>
        </div>

        <div style={{ background: 'rgba(0,0,0,0.3)', padding: '12px', borderRadius: '8px', border: '1px solid rgba(255,255,255,0.05)' }}>
          <div style={{ fontSize: '0.75rem', color: '#8a99ad' }}>MOTOR CURRENT</div>
          <div style={{ fontSize: '1.2rem', fontWeight: 700, color: '#ffaa00', fontFamily: 'JetBrains Mono', marginTop: '4px' }}>
            {vehicleStatus?.motor_current_a || 0.8} A
          </div>
        </div>
      </div>
    </div>
  );
};
