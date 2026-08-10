from enum import IntEnum
from dataclasses import dataclass
from typing import List, Optional

class AlertLevel(IntEnum):
    L0_NORMAL = 0
    L1_EARLY = 1
    L2_DROWSY = 2
    L3_DANGER = 3

class DomainState(IntEnum):
    IDLE = 0
    ACTIVE = 1
    SEVERE = 2
    CRITICAL = 3

@dataclass
class FaceLandmarks:
    timestamp_ms: int
    left_eye: List[tuple]
    right_eye: List[tuple]
    lips: List[tuple]
    yaw: float
    pitch: float
    confidence: float