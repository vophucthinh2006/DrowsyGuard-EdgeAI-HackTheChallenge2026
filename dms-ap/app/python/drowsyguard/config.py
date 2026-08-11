"""Loads config/thresholds.yaml into typed, immutable config objects.

specs/02-development-standards.md DEV-050/DEV-043: thresholds.yaml is the
single source of truth for every domain/fusion number; nothing in
domains/ or fusion/ hard-codes a threshold. This module is the only place
that reads the YAML file.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any

import yaml

DEFAULT_THRESHOLDS_PATH = Path(__file__).resolve().parents[1] / "config" / "thresholds.yaml"


@dataclass(frozen=True, slots=True)
class D1Config:
    yaw_limit_deg: float
    pitch_down_limit_deg: float
    glance_min_ms: float
    active_dwell_ms: float
    severe_cum_ms: float
    cum_window_ms: float
    clear_dwell_ms: float
    indicator_suppress_ms: float


@dataclass(frozen=True, slots=True)
class D2Config:
    mar_open: float
    yawn_min_ms: float
    yawn_max_ms: float
    window_ms: float
    active_count: int
    severe_count: int
    severe_single_ms: float


@dataclass(frozen=True, slots=True)
class D3Config:
    active_dwell_ms: float
    severe_dwell_ms: float
    critical_dwell_ms: float
    clear_dwell_ms: float
    eye_conf_min: float
    perclos_window_ms: float
    perclos_p: float
    perclos_min_samples: int
    perclos_active: float
    perclos_severe: float
    perclos_hyst: float


@dataclass(frozen=True, slots=True)
class FusionConfig:
    l2_escalate_ms: float
    level_clear_ms: float
    ack_refractory_ms: float
    ack_max_consecutive: int


@dataclass(frozen=True, slots=True)
class FaultConfig:
    sensor_lost_ms: float


@dataclass(frozen=True, slots=True)
class Thresholds:
    d1: D1Config
    d2: D2Config
    d3: D3Config
    fusion: FusionConfig
    fault: FaultConfig


def _values(section: dict[str, Any]) -> dict[str, Any]:
    """Strips the {value, rationale} wrapper down to {field: value}."""
    return {key: entry["value"] for key, entry in section.items()}


def load_thresholds(path: Path | str = DEFAULT_THRESHOLDS_PATH) -> Thresholds:
    with open(path, encoding="utf-8") as f:
        raw = yaml.safe_load(f)

    return Thresholds(
        d1=D1Config(**_values(raw["d1_distraction"])),
        d2=D2Config(**_values(raw["d2_yawn"])),
        d3=D3Config(**_values(raw["d3_eye_closure"])),
        fusion=FusionConfig(**_values(raw["fusion"])),
        fault=FaultConfig(**_values(raw["fault"])),
    )
