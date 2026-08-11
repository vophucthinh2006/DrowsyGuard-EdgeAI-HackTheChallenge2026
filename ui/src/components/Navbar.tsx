import React from 'react';
import { LayoutDashboard, Camera, ShieldAlert, Wifi, WifiOff } from 'lucide-react';

interface NavbarProps {
  activeTab: 'dashboard' | 'camera';
  onTabChange: (tab: 'dashboard' | 'camera') => void;
  isConnected: boolean;
  alertLevel: number;
}

export const Navbar: React.FC<NavbarProps> = ({
  activeTab,
  onTabChange,
  isConnected,
  alertLevel,
}) => {
  const getAlertBadgeColor = (level: number) => {
    switch (level) {
      case 0: return '#00ff88';
      case 1: return '#ffaa00';
      case 2: return '#ff0055';
      case 3: return '#ff0000';
      default: return '#8a99ad';
    }
  };

  const alertColor = getAlertBadgeColor(alertLevel);

  return (
    <header className="navbar">
      {/* Brand */}
      <div className="nav-brand">
        <ShieldAlert size={26} style={{ color: '#00f2fe' }} />
        <span>DROWSYGUARD</span>
        <span style={{ fontSize: '0.75rem', fontWeight: 600, opacity: 0.6, letterSpacing: '1px' }}>
          EDGE-AI COCKPIT
        </span>
      </div>

      {/* Center Tabs Navigation */}
      <div className="nav-tabs">
        <button
          className={`nav-tab ${activeTab === 'dashboard' ? 'active' : ''}`}
          onClick={() => onTabChange('dashboard')}
        >
          <LayoutDashboard size={18} />
          <span>Dashboard</span>
        </button>
        <button
          className={`nav-tab ${activeTab === 'camera' ? 'active' : ''}`}
          onClick={() => onTabChange('camera')}
        >
          <Camera size={18} />
          <span>Camera Feed & Vision</span>
        </button>
      </div>

      {/* Right Connection & Alert Status */}
      <div style={{ display: 'flex', alignItems: 'center', gap: '16px' }}>
        {/* Alert Level Pill */}
        <div
          style={{
            background: `${alertColor}20`,
            color: alertColor,
            border: `1px solid ${alertColor}50`,
            padding: '4px 12px',
            borderRadius: '16px',
            fontWeight: 800,
            fontSize: '0.8rem',
            display: 'flex',
            alignItems: 'center',
            gap: '6px',
          }}
        >
          <span style={{ width: 8, height: 8, borderRadius: '50%', background: alertColor, boxShadow: `0 0 8px ${alertColor}` }} />
          L{alertLevel} ALERT
        </div>

        {/* WebSocket Status */}
        <div className={`status-badge ${isConnected ? 'connected' : 'disconnected'}`}>
          {isConnected ? <Wifi size={14} /> : <WifiOff size={14} />}
          <span>{isConnected ? 'ONLINE' : 'OFFLINE (SIM)'}</span>
        </div>
      </div>
    </header>
  );
};
