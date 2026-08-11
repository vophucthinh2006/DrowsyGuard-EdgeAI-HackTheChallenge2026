import React from 'react';
import { Speedometer } from '../components/Speedometer';
import { RpmGauge } from '../components/RpmGauge';
import { DriverStateCard } from '../components/DriverStateCard';
import { DevPanel } from '../components/DevPanel';
import { DriverStatusPacket, VehicleStatusPacket } from '../types/websocket';
import { ArrowLeft, ArrowRight, AlertTriangle, Sun, Disc } from 'lucide-react';

interface DashboardPageProps {
  driverStatus: DriverStatusPacket | null;
  vehicleStatus: VehicleStatusPacket | null;
  latencyMs: number;
  isConnected: boolean;
  onToggleSim: () => void;
}

export const DashboardPage: React.FC<DashboardPageProps> = ({
  driverStatus,
  vehicleStatus,
  latencyMs,
  isConnected,
  onToggleSim,
}) => {
  const speedKmh = vehicleStatus?.speed_kmh || 0;
  const speedCapPct = vehicleStatus?.speed_cap_pct ?? 100;
  const rpm = vehicleStatus?.rpm || 0;

  const indicators = vehicleStatus?.indicators || {
    turn_left: false,
    turn_right: false,
    hazard: false,
    headlights: true,
    seatbelt: true,
  };

  return (
    <div className="page-content">
      <div className="dashboard-grid">
        {/* Left Column: Gauges & Vehicle Telemetry */}
        <div>
          <div className="glass-panel" style={{ padding: '24px' }}>
            <h3 style={{ fontSize: '1.2rem', fontWeight: 700, marginBottom: '16px', color: '#f0f4fd' }}>
              Instrument Cluster Cockpit
            </h3>

            {/* Dials */}
            <div className="gauges-container">
              <Speedometer speedKmh={speedKmh} speedCapPct={speedCapPct} />
              <RpmGauge rpm={rpm} />
            </div>

            {/* Indicator Icon Bar */}
            <div className="indicator-bar">
              <div className={`indicator-icon ${indicators.turn_left ? 'active-left' : ''}`}>
                <ArrowLeft size={20} />
              </div>
              <div className={`indicator-icon ${indicators.hazard ? 'active-hazard' : ''}`}>
                <AlertTriangle size={20} />
              </div>
              <div className={`indicator-icon ${indicators.headlights ? 'active-headlights' : ''}`}>
                <Sun size={20} />
              </div>
              <div className={`indicator-icon ${indicators.seatbelt ? 'active-headlights' : ''}`}>
                <Disc size={20} />
              </div>
              <div className={`indicator-icon ${indicators.turn_right ? 'active-right' : ''}`}>
                <ArrowRight size={20} />
              </div>
            </div>

            {/* Odometer & Status */}
            <div style={{ display: 'flex', justifyContent: 'space-between', marginTop: '20px', padding: '12px 16px', background: 'rgba(0,0,0,0.2)', borderRadius: '8px' }}>
              <div>
                <span style={{ fontSize: '0.8rem', color: '#8a99ad' }}>ODOMETER</span>
                <div style={{ fontFamily: 'JetBrains Mono', fontWeight: 700, fontSize: '1.1rem', color: '#00f2fe' }}>
                  {(vehicleStatus?.odometer_km || 1248.5).toFixed(1)} KM
                </div>
              </div>
              <div style={{ textAlign: 'right' }}>
                <span style={{ fontSize: '0.8rem', color: '#8a99ad' }}>VEHICLE STATE</span>
                <div style={{ fontWeight: 800, fontSize: '1.1rem', color: vehicleStatus?.vehicle_state === 'DECEL' ? '#ff0055' : '#00ff88' }}>
                  {vehicleStatus?.vehicle_state || 'RUN'}
                </div>
              </div>
            </div>
          </div>
        </div>

        {/* Right Column: Driver Risk Metrics */}
        <div>
          <DriverStateCard status={driverStatus} />
        </div>
      </div>

      {/* Developer Telemetry & Control Panel */}
      <DevPanel
        driverStatus={driverStatus}
        vehicleStatus={vehicleStatus}
        latencyMs={latencyMs}
        isConnected={isConnected}
        onToggleSim={onToggleSim}
      />
    </div>
  );
};
