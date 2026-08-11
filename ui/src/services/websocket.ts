import { AnyPacket, CameraFramePacket, DriverStatusPacket, VehicleStatusPacket } from '../types/websocket';

export type PacketCallback = (packet: AnyPacket) => void;
export type StatusCallback = (connected: boolean, latencyMs: number) => void;

export class WebSocketService {
  private ws: WebSocket | null = null;
  private url: string = 'ws://localhost:8888';
  private packetCallbacks: PacketCallback[] = [];
  private statusCallbacks: StatusCallback[] = [];
  private isConnected: boolean = false;
  private latencyMs: number = 0;
  private isSimulating: boolean = false;
  private simInterval: number | null = null;
  private simTickCount: number = 0;

  constructor(url?: string) {
    if (url) this.url = url;
  }

  public connect(url?: string): void {
    if (url) this.url = url;
    if (this.isSimulating) this.stopSimulation();

    try {
      this.ws = new WebSocket(this.url);
      this.ws.binaryType = 'arraybuffer';

      this.ws.onopen = () => {
        this.isConnected = true;
        this.notifyStatus();
      };

      this.ws.onmessage = (event) => {
        try {
          let dataText: string;
          if (typeof event.data === 'string') {
            dataText = event.data;
          } else {
            const dec = new TextDecoder('utf-8');
            dataText = dec.decode(new Uint8Array(event.data));
          }
          const packet: AnyPacket = JSON.parse(dataText);
          if (packet.ts) {
            this.latencyMs = Math.max(0, Math.round(Date.now() - packet.ts * 1000));
          }
          this.notifyPacket(packet);
          this.notifyStatus();
        } catch (e) {
          console.warn('Failed to parse WS packet:', e);
        }
      };

      this.ws.onerror = () => {
        this.isConnected = false;
        this.notifyStatus();
      };

      this.ws.onclose = () => {
        this.isConnected = false;
        this.notifyStatus();
        // Fallback to simulation if connection refused
        if (!this.isSimulating) {
          console.log('WS connection closed. Auto-starting Simulation Mode for demo...');
          this.startSimulation();
        }
      };
    } catch (e) {
      console.warn('WS Connect Error:', e);
      this.startSimulation();
    }
  }

  public disconnect(): void {
    this.stopSimulation();
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
    this.isConnected = false;
    this.notifyStatus();
  }

  public onPacket(cb: PacketCallback): () => void {
    this.packetCallbacks.push(cb);
    return () => {
      this.packetCallbacks = this.packetCallbacks.filter((c) => c !== cb);
    };
  }

  public onStatus(cb: StatusCallback): () => void {
    this.statusCallbacks.push(cb);
    cb(this.isConnected || this.isSimulating, this.latencyMs);
    return () => {
      this.statusCallbacks = this.statusCallbacks.filter((c) => c !== cb);
    };
  }

  private notifyPacket(packet: AnyPacket): void {
    this.packetCallbacks.forEach((cb) => cb(packet));
  }

  private notifyStatus(): void {
    const active = this.isConnected || this.isSimulating;
    this.statusCallbacks.forEach((cb) => cb(active, this.latencyMs));
  }

  /* ---------------- Simulation Mode Generator ---------------- */
  public startSimulation(): void {
    if (this.isSimulating) return;
    this.isSimulating = true;
    this.notifyStatus();

    this.simInterval = window.setInterval(() => {
      this.simTickCount++;
      const now = Date.now() / 1000;
      this.latencyMs = Math.floor(12 + Math.random() * 8);

      // Cycle simulation alert states every 15 seconds
      const cycle = Math.floor(this.simTickCount / 150) % 5;
      let alertLevel = 0; // 0=L0, 1=L1, 2=L2, 3=L3, 4=SENSOR_LOST
      let alertName = 'L0_NORMAL';
      let faceDetected = true;
      let eyeClosureMs = 120;
      let yawnCount = 0;
      let perclos = 4;
      let eorCum = 300;

      if (cycle === 1) {
        alertLevel = 1; alertName = 'L1_EARLY'; perclos = 9; eyeClosureMs = 850;
      } else if (cycle === 2) {
        alertLevel = 2; alertName = 'DROWSY_L2'; perclos = 14; eyeClosureMs = 1650; yawnCount = 2; eorCum = 2500;
      } else if (cycle === 3) {
        alertLevel = 3; alertName = 'L3_DANGER'; perclos = 22; eyeClosureMs = 3100; yawnCount = 3;
      } else if (cycle === 4) {
        faceDetected = false; // NO FACE DETECTED!
      }

      // 1. Camera Frame Packet
      const cameraPacket: CameraFramePacket = {
        type: 'camera_frame',
        ts: now,
        image_width: 640,
        image_height: 480,
        fps: 10.1,
        inference_ms: 76,
        face_detected: faceDetected,
        face_confidence: faceDetected ? 0.95 : 0.0,
        bounding_boxes: faceDetected
          ? {
              face: { x: 230, y: 110, width: 180, height: 220 },
              left_eye: { x: 260, y: 155, width: 40, height: 22, closed: alertLevel >= 2, ear: alertLevel >= 2 ? 0.12 : 0.31 },
              right_eye: { x: 340, y: 157, width: 40, height: 22, closed: alertLevel >= 2, ear: alertLevel >= 2 ? 0.13 : 0.30 },
              mouth: { x: 290, y: 240, width: 60, height: yawnCount > 1 ? 45 : 25, yawning: yawnCount > 1, mar: yawnCount > 1 ? 0.65 : 0.22 },
            }
          : {},
        head_pose: {
          pitch: alertLevel === 1 ? -12 : 2.5,
          yaw: alertLevel === 2 ? 28 : -1.2,
          roll: 0.4,
        },
      };

      // 2. Driver Status Packet
      const driverPacket: DriverStatusPacket = {
        type: 'driver_status',
        ts: now,
        seq: (this.simTickCount % 16),
        alert_level: alertLevel === 4 ? 1 : alertLevel,
        alert_name: alertName,
        d1_state: alertLevel >= 2 ? 'ACTIVE' : 'IDLE',
        d2_state: yawnCount >= 2 ? 'ACTIVE' : 'IDLE',
        d3_state: alertLevel === 3 ? 'CRITICAL' : alertLevel === 2 ? 'SEVERE' : alertLevel === 1 ? 'ACTIVE' : 'IDLE',
        d3_available: faceDetected ? 'AVAILABLE' : 'UNAVAILABLE',
        perclos_pct: perclos,
        perclos_threshold_severe: 15,
        eye_closure_ms: eyeClosureMs,
        eye_closure_threshold_critical: 3000,
        yawn_count: yawnCount,
        yawn_threshold_severe: 3,
        eor_cum_ms: eorCum,
        eor_threshold_severe: 6000,
        face_conf_pct: faceDetected ? 95 : 0,
        sensor_lost_duration_ms: faceDetected ? 0 : 3500,
        flags: {
          ack_refractory: false,
          sensor_lost: !faceDetected,
          model_degraded: false,
          night_mode: false,
          calib_done: true,
          ack_saturated: false,
          pipeline_slow: false,
        },
      };

      // 3. Vehicle Status Packet
      const vehiclePacket: VehicleStatusPacket = {
        type: 'vehicle_status',
        ts: now,
        vehicle_state: alertLevel === 3 ? 'DECEL' : 'RUN',
        speed_kmh: alertLevel === 3 ? Math.max(0, 75 - (this.simTickCount % 10) * 8) : alertLevel === 2 ? 45 : 85,
        speed_cap_pct: alertLevel === 3 ? 0 : alertLevel === 2 ? 50 : alertLevel === 1 ? 80 : 100,
        rpm: alertLevel === 3 ? 0 : 2600,
        odometer_km: 1248.5 + (this.simTickCount * 0.002),
        duty_left_pct: alertLevel === 3 ? 0 : 60,
        duty_right_pct: alertLevel === 3 ? 0 : 60,
        battery_voltage_v: 7.38,
        logic_supply_v: 5.02,
        motor_current_a: alertLevel === 3 ? 0.1 : 0.82,
        indicators: {
          turn_left: false,
          turn_right: false,
          hazard: alertLevel === 3,
          headlights: true,
          seatbelt: true,
        },
        actuators: {
          buzzer_active: alertLevel >= 1,
          buzzer_freq_hz: alertLevel === 3 ? 3200 : alertLevel === 2 ? 2800 : 2000,
          vibration_active: alertLevel >= 2,
          fan_relay_active: alertLevel >= 2,
          status_led: alertLevel === 3 ? 'RED_FLASH' : alertLevel === 2 ? 'RED' : alertLevel === 1 ? 'AMBER' : 'GREEN',
        },
        faults: {
          driver_fault: false,
          watchdog_reset: false,
          can_timeout: false,
          undervoltage: false,
          estop_active: false,
        },
      };

      this.notifyPacket(cameraPacket);
      this.notifyPacket(driverPacket);
      this.notifyPacket(vehiclePacket);
    }, 100); // 10 Hz simulation loop
  }

  public stopSimulation(): void {
    if (this.simInterval) {
      clearInterval(this.simInterval);
      this.simInterval = null;
    }
    this.isSimulating = false;
    this.notifyStatus();
  }
}

export const wsService = new WebSocketService();
