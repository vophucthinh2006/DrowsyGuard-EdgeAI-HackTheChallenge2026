# Summary of 03 — Drowsiness Domain Specification

Source: [specs/03-drowsiness-domain-spec.md](../../specs/03-drowsiness-domain-spec.md)
*(The most important document algorithmically — contains every threshold and the reasoning behind it)*

## Design philosophy (3 core ideas)
1. The three signals are **not the same kind of evidence**: D1 = behavioural (awake but not
   looking), D2 = predictive (fatigue building), D3 = imminent danger (unconscious right now).
   That's why each domain has its own detector, timers, and its own severity ceiling — sharing
   one threshold would make yawns alarming or make microsleeps detected too late.
2. **Dwell time is the false-alarm defence, and it costs latency.** Every threshold is paired
   with how long the condition must hold — this is what separates a blink from a microsleep.
3. **A false alarm is not free.** A driver woken by a wrong alarm learns to ignore it — a system
   that "cries wolf" is worse than no system at all. This is why the false-alarm rate is a
   **hard requirement** (SYS-SR-008), not an aspiration.

## Domain overview
| ID | Domain | Signal class | Time character | Max level reachable alone |
|---|---|---|---|---|
| D1 | Distraction | Behavioural | Seconds | L2 |
| D2 | Yawning | Predictive | Minutes | L2 |
| D3 | Eye closure | Imminent danger | Sub-second to seconds | **L3** |

**Only D3 may push the system to L3** on its own. D1 and D2 are always capped at L2 no matter
how severe or prolonged — because L3 stops the vehicle, and only "eyes shut for seconds" justifies
"this driver is not in control."

## D1 — Distraction
- `off_road` = |yaw|>30° OR pitch-down >20° OR face present but both eyes not visible.
- Main thresholds: **2000ms** continuous → ACTIVE; **6000ms cumulative in 12s** → SEVERE (or 4s
  continuous).
- Noise floor 600ms (ignores normal mirror/instrument-cluster glances).
- Clear dwell 3000ms (harder to leave the warning state than to enter it).
- While the simulated turn indicator is on: **only the indicated direction** is exempted from
  yaw detection — pitch-down is **never** exempted.
- Provenance: NHTSA's 2s single-glance guideline, the VTTI 100-Car study (>2s off-road ≈ 2×
  crash/near-crash risk). At 90 km/h, 2 seconds is 50 meters travelled with no eyes on the road.
- **Known limitation:** D1 uses head pose as a proxy for gaze — a driver keeping their head
  forward but looking elsewhere won't be caught. This limitation must always be stated when
  reporting D1 performance.

## D2 — Yawning
- Measures mouth-open via MAR (Mouth Aspect Ratio) > 0.60.
- A valid yawn event lasts **1500ms–12000ms**. Below 1500ms is treated as speech/laughter/eating
  (speech opens the mouth in 150-250ms bursts); above 12000ms is treated as a stuck detection —
  discarded and flags `model_degraded`.
- **2 yawns in 2 minutes** → ACTIVE; **3 yawns in 2 minutes** (or 1 yawn ≥5000ms) → SEVERE.
- A single yawn **never** triggers an alarm — a deliberate sensitivity trade-off: the first time
  the system beeps for one yawn, it has lost that driver's trust.
- Ack does **not** reset the yawn counter — ack only silences the alert, it doesn't make the
  driver less tired.

## D3 — Eye closure (the domain that justifies the product)
Two independent sub-signals, **both required**:

### (a) Continuous closure
| Threshold | Value | Meaning |
|---|---|---|
| ACTIVE | **800ms** | ~2× the upper bound of a normal blink (100-400ms) — cannot be triggered by any normal blink at any blink rate |
| SEVERE | **1500ms** | The standard clinical/automotive microsleep marker. At 90km/h × 1.5s = 37.5m with nobody driving |
| CRITICAL | **3000ms** | "Assume no driver." At 90km/h × 3s = 100m. This is the project's headline number |

- The time the driver *actually experiences* = dwell + 1/FPS + pipeline latency:
  ACTIVE≈1.06s, SEVERE≈1.76s, CRITICAL≈3.26s. **These are the numbers allowed to be quoted
  publicly**, not the bare dwell time.
- Low-confidence frames (<0.50) cause the accumulator to **hold** (neither reset nor increment)
  — avoiding both failure modes: resetting on noise cancels a genuine microsleep, incrementing
  on it lets a camera obstruction fabricate a fake event.

### (b) PERCLOS (trend)
- % of time eyes are ≥80% closed over a rolling **60-second** window. The most-validated fatigue
  metric in the literature (originates from FHWA/Wierwille).
- **8%** → ACTIVE-TREND, **15%** → SEVERE-TREND, 2% hysteresis.
- Needs ≥300 valid frames before publishing a value, otherwise INVALID (0xFF on CAN),
  contributing to no state.
- **PERCLOS can never drive CRITICAL** — a high PERCLOS means impairment over the last minute,
  not unconsciousness right now; only sub-signal (a) proves that.

The two sub-signals cover each other's blind spot: (a) catches a single dangerous event but is
"blind" to gradual decline; (b) catches gradual decline but reacts slowly (up to 60s), missing
the first microsleep.

- Effective D3 state = **max** of (a) and (b).
- D3 CRITICAL **cannot be cleared by ack** — it only clears when eyes are observed open
  continuously for 1000ms.
- If dark sunglasses are detected, or confidence is low >30% of the last 3s → D3 = **UNAVAILABLE**,
  sets `model_degraded`, holds at L1 with a distinct indication — **must never report IDLE** in
  this state (rationale: "I can't see the eyes" reported as "eyes are open" is the single most
  dangerous software defect this system could have).

## Fusion — the alert ladder
| Level | Entry condition |
|---|---|
| L0 | All domains IDLE |
| L1 | Exactly one domain ACTIVE |
| L2 | Any domain SEVERE, **OR** ≥2 domains ACTIVE simultaneously |
| L3 | D3 CRITICAL, **OR** L2 sustained >10s with no ack |

- The "2 domains ACTIVE = L2" rule is why the 3-domain architecture earns its keep: corroboration
  across independent evidence lets each individual threshold stay conservative (low false-alarm
  rate) while the system stays sensitive.
- L2→L3 after 10s of no ack: long enough for an alert driver to have pressed the button, short
  enough that at 90km/h it hasn't already been 250m.
- Ack clears L1/L2 to L0, starts a 60s refractory (blocks L1 re-entry, **not** L2). Ack has no
  effect at L3. After **3 consecutive acks** within 10 minutes with no sustained return to L0 →
  `ack_saturated`, refractory disabled (prevents the ack button from becoming a permanent snooze).
- De-escalation: requires **all** domains IDLE continuously for 5s, and steps down **one level
  at a time** (never straight to L0).
- L3 **never auto-de-escalates** — only exits via explicit operator re-arm.

## Fault/degraded states (not drowsiness states, must be clearly distinguished)
- `SENSOR_LOST`: no face for >3000ms while armed → L1-equivalent but **silent, no drowsiness sound**.
- `MODEL_DEGRADED`: low eye confidence / overly long D2 episode / sunglasses detected.
- `PIPELINE_SLOW`: FPS<5 for >5s.
- Dwell is always computed from **wall-clock timestamps**, never frame counts — even at 3 FPS,
  an 800ms closure must be declared at 800ms of real time, not after N frames.
- A fault state must **never** be silently mapped to L0.

## Other important notes
- Every threshold here is a **literature-derived prior**, not an experimental measurement —
  must be re-tuned on the team's own corpus before acceptance.
- `D3_ACTIVE_MS/SEVERE_MS/CRITICAL_MS` must not be lowered below the current values without
  evidence from the test population's measured blink-duration distribution — these are the
  three numbers a judge is most likely to challenge.
- Citations R1-R6 (Wierwille/PERCLOS, VTTI 100-Car, NHTSA, blink duration, yawn duration) are
  currently only "reasoning basis," **not yet verified against primary sources** — must be done
  before any public claim.
