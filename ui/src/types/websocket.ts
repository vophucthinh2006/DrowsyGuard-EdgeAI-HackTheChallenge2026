export type PacketType = 'camera_frame' | 'driver_status' | 'vehicle_status';

export interface BoundingBox {
  x: number;
  y: number;
  width: number;
  height: number;
  closed?: boolean;
  yawning?: boolean;
  ear?: number;
  mar?: number;
}

export interface LandmarkPoint {
  x: number;
  y: number;
  z?: number;
}

export interface CameraFramePacket {
  type: 'camera_frame';
  ts: number;
  frame_jpeg?: string;
  image_width: number;
  image_height: number;
  fps: number;
  inference_ms: number;
  face_detected: boolean;
  face_confidence: number;
  bounding_boxes: {
    face?: BoundingBox;
    left_eye?: BoundingBox;
    right_eye?: BoundingBox;
    mouth?: BoundingBox;
  };
  head_pose: {
    pitch: number;
    yaw: number;
    roll: number;
  };
  landmarks?: LandmarkPoint[];
}

export interface DriverStatusPacket {
  type: 'driver_status';
  ts: number;
  seq: number;
  alert_level: number; // 0=L0, 1=L1, 2=L2, 3=L3
  alert_name: string; // L0_NORMAL, L1_EARLY, L2_DROWSY, L3_DANGER
  d1_state: 'IDLE' | 'ACTIVE' | 'SEVERE';
  d2_state: 'IDLE' | 'ACTIVE' | 'SEVERE';
  d3_state: 'IDLE' | 'ACTIVE' | 'SEVERE' | 'CRITICAL';
  d3_available: 'AVAILABLE' | 'DEGRADED' | 'UNAVAILABLE';
  perclos_pct: number;
  perclos_threshold_severe: number;
  eye_closure_ms: number;
  eye_closure_threshold_critical: number;
  yawn_count: number;
  yawn_threshold_severe: number;
  eor_cum_ms: number;
  eor_threshold_severe: number;
  face_conf_pct: number;
  sensor_lost_duration_ms: number;
  flags: {
    ack_refractory: boolean;
    sensor_lost: boolean;
    model_degraded: boolean;
    night_mode: boolean;
    calib_done: boolean;
    ack_saturated: boolean;
    pipeline_slow: boolean;
  };
}

export interface VehicleStatusPacket {
  type: 'vehicle_status';
  ts: number;
  vehicle_state: string; // INIT, DISARMED, ARMED_IDLE, RUN, LIMITED, DECEL, STOPPED, ESTOP, FAULT
  speed_kmh: number;
  speed_cap_pct: number;
  rpm: number;
  odometer_km: number;
  duty_left_pct: number;
  duty_right_pct: number;
  battery_voltage_v: number;
  logic_supply_v: number;
  motor_current_a: number;
  indicators: {
    turn_left: boolean;
    turn_right: boolean;
    hazard: boolean;
    headlights: boolean;
    seatbelt: boolean;
  };
  actuators: {
    buzzer_active: boolean;
    buzzer_freq_hz: number;
    vibration_active: boolean;
    fan_relay_active: boolean;
    status_led: string;
  };
  faults: {
    driver_fault: boolean;
    watchdog_reset: boolean;
    can_timeout: boolean;
    undervoltage: boolean;
    estop_active: boolean;
  };
}

export type AnyPacket = CameraFramePacket | DriverStatusPacket | VehicleStatusPacket;
