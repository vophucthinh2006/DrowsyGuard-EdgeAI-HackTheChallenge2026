"""Shared types for the domain and fusion layer.

specs/02-development-standards.md DEV-042: domains and fusion are pure --
timestamped observations in, state out. No camera handle, no clock read, no
I/O anywhere in domains/ or fusion/. Time is always passed in explicitly as
`now_ms`, which is what makes a 30-minute annotated corpus replay through
this exact production logic deterministically in well under a second.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum


class DomainState(IntEnum):
    """The common ladder every domain state machine climbs (spec 03 §2).

    CRITICAL is only ever produced by D3 (DOM-000) -- D1Distraction and
    D2Yawn never return it, enforced by their own state machines rather than
    by this shared enum, so misuse still shows up as a specific, readable
    assertion at the call site instead of a silent cap.
    """

    IDLE = 0
    ACTIVE = 1
    SEVERE = 2
    CRITICAL = 3


@dataclass(frozen=True, slots=True)
class FrameObservation:
    """One frame's worth of raw (pre-domain-logic) evidence.

    Produced by an InferenceBackend (see inference/backend.py), consumed by
    the three domain modules. Every field is either a direct model output or
    a simple geometric/derived quantity -- no domain threshold (dwell time,
    window, count) is ever applied before this point; that is the domain
    modules' entire job.
    """

    timestamp_ms: float
    face_present: bool
    face_confidence: float  # 0..1; meaningless if not face_present

    # D3 (eye closure). None = not classifiable this frame (occlusion,
    # extreme angle, sunglasses) -- see DOM-D3-001/008: a None frame HOLDS
    # the D3 continuous-closure accumulator, it does not reset or fabricate.
    eye_closed: bool | None
    eye_confidence: float  # 0..1, see inference backend docstring for how this is derived
    sunglasses_detected: bool

    # D2 (yawn). None = not classifiable (mouth occluded/out of frame).
    mouth_open: bool | None
    mouth_confidence: float

    # D1 (distraction). None = not implemented yet on this inference
    # backend (see inference/blazeface_cnn_backend.py) -- BlazeFace's 6
    # keypoints alone don't give a validated head-pose estimate without a
    # 3D face model + camera intrinsics, which hasn't been built. D1 treats
    # None as "no evidence this frame", matching how it already treats a
    # missing face.
    yaw_deg: float | None
    pitch_deg: float | None

    # From the VCS over the DMS<->VCS link (CAN-030) -- not implemented
    # yet, always False/0 until that link exists. See link/README notes.
    indicator_active: bool = False
    indicator_dir: int = 0  # 0 none, 1 left, 2 right
