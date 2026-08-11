import React, { useState, useEffect } from 'react';
import { Navbar } from './components/Navbar';
import { DashboardPage } from './pages/DashboardPage';
import { CameraMonitorPage } from './pages/CameraMonitorPage';
import { AlertOverlay } from './components/AlertOverlay';
import { wsService } from './services/websocket';
import { CameraFramePacket, DriverStatusPacket, VehicleStatusPacket } from './types/websocket';

export const App: React.FC = () => {
  const [activeTab, setActiveTab] = useState<'dashboard' | 'camera'>('dashboard');
  const [isConnected, setIsConnected] = useState<boolean>(false);
  const [latencyMs, setLatencyMs] = useState<number>(0);

  const [cameraFrame, setCameraFrame] = useState<CameraFramePacket | null>(null);
  const [driverStatus, setDriverStatus] = useState<DriverStatusPacket | null>(null);
  const [vehicleStatus, setVehicleStatus] = useState<VehicleStatusPacket | null>(null);

  useEffect(() => {
    // 1. Connect to WebSocket
    wsService.connect();

    // 2. Listen to Status
    const unsubStatus = wsService.onStatus((connected, latency) => {
      setIsConnected(connected);
      setLatencyMs(latency);
    });

    // 3. Listen to Packets
    const unsubPacket = wsService.onPacket((packet) => {
      if (packet.type === 'camera_frame') {
        setCameraFrame(packet);
      } else if (packet.type === 'driver_status') {
        setDriverStatus(packet);
      } else if (packet.type === 'vehicle_status') {
        setVehicleStatus(packet);
      }
    });

    return () => {
      unsubStatus();
      unsubPacket();
      wsService.disconnect();
    };
  }, []);

  const handleToggleSimulation = () => {
    wsService.startSimulation();
  };

  return (
    <div className="app-container">
      {/* Universal Alert Overlay */}
      <AlertOverlay status={driverStatus} />

      {/* Top Navbar Header */}
      <Navbar
        activeTab={activeTab}
        onTabChange={setActiveTab}
        isConnected={isConnected}
        alertLevel={driverStatus?.alert_level || 0}
      />

      {/* Tab Router Content */}
      {activeTab === 'dashboard' ? (
        <DashboardPage
          driverStatus={driverStatus}
          vehicleStatus={vehicleStatus}
          latencyMs={latencyMs}
          isConnected={isConnected}
          onToggleSim={handleToggleSimulation}
        />
      ) : (
        <CameraMonitorPage
          cameraFrame={cameraFrame}
          driverStatus={driverStatus}
        />
      )}
    </div>
  );
};

export default App;
