# 03 — Drowsiness Domain Specification

**Document:** DG-SPEC-03 · Rev 0.1 · 2026-08-10 · DRAFT
**Applies to:** `dms-ap/app/python/drowsyguard/domains/` and `fusion/`
**Normative:** every table marked **NORMATIVE** is machine-checked against
`config/thresholds.yaml` by CI (see [02 §6](02-development-standards.md#6-configuration-and-the-threshold-single-source-rule))

---

## 0. Implementation status — what is actually running today (added 2026-08-16)

Everything below §1 describes the **target design** for the full three-domain fusion
pipeline (`dms-ap/app/python/drowsyguard/domains/` + `fusion/`, in the canonical
`DrowsyGuard-EdgeAI-HackTheChallenge2026/dms-ap/` tree). **That pipeline is not what is
deployed and running on real hardware today.** The only camera-driven, on-hardware
implementation that currently exists is
`qualcomm_AI/MAIN_DMS_YOLOX_System/copy-of-object-hunting/` (`python/main.py` +
`sketch/sketch.ino`), and it is a substantially reduced subset of this spec:

- **No D1 (distraction).** The deployed model (Edge Impulse-trained YOLOX Nano,
  `ei-model-1086456-7`, see `app.yaml`) outputs three per-frame class labels —
  `closed_eye`, `open_eye`, `yawning` — with confidence scores only. There are no face
  landmarks and no head-pose (yaw/pitch) output, so §3's yaw/pitch-based `off_road()`
  detector has no signal to run on. Distraction is not detected at all in the deployed
  system.
- **No PERCLOS sub-signal.** §5.2 (the 60 s rolling P80 window) is not computed anywhere
  in the deployed code.
- **No MAR-based mouth measurement.** §4.1's mouth-aspect-ratio detector does not exist;
  "yawning" is a single model class, gated on confidence and duration only (see below).
- **No fusion ladder (§6.1).** The deployed MCU (`sketch.ino`) computes `alert_level`
  directly from raw eye-closed/yawn duration with a simple priority `if`/`else if` chain
  (L3 if eyes closed ≥ 4.0 s, else L2 if ≥ 2.0 s, else L1 if ≥ 1.0 s, else L1 if yawning
  ≥ 1.5 s) — not the "≥2 domains ACTIVE ⇒ L2" / "L2 sustained 10 s ⇒ L3" corroboration
  rule in §6.1, since there is only ever at most one domain (eye-closure) driving L2/L3.
- **Different D3a timing values than §5.1/§8.** The deployed thresholds are: L1-equivalent
  (`TARGET_EYE_WARN_MS`) = **1000 ms**, L2-equivalent (`TARGET_EYE_ALARM_MS`) =
  **2000 ms**, L3-equivalent (`TARGET_EYE_DANGER_MS`) = **4000 ms** — vs. this spec's
  800/1500/3000 ms. These have **not** been reconciled against the blink-physiology
  literature in §5.1/§10; they are current shipped values, not a revision of the
  research-derived priors. Yawn gate (`TARGET_YAWN_WARN_MS` = 1500 ms) does match
  `YAWN_MIN_MS`.
- **Timing is real-time, matching DOM-FLT-001's intent, at different absolute values.**
  `sketch.ino` tracks closed/yawn duration with `millis()` timestamps
  (`eyeClosedSinceMs`/`yawnSinceMs`), not frame counts — consistent with DOM-FLT-001 —
  so the §5.1 "What the driver actually experiences" table's `+ frame quantisation`
  term does not apply to the deployed system; time-to-alert there is simply
  `dwell + pipeline_latency`, no extra 100 ms term.
- **Ack is unconditional, no refractory.** `dismiss_alert` (triggered by a screen tap)
  resets the eye/yawn counters immediately and silences the buzzer with no
  `ACK_REFRACTORY_MS` / `ACK_MAX_CONSECUTIVE` bookkeeping (§6.4 DOM-FUS-001/003 are not
  implemented).
- **An extra confidence-filtering layer this spec doesn't describe.** Detections are
  filtered *twice*: `main.py`'s `CLASS_THRESHOLDS` (closed_eye ≥ 0.35, yawning ≥ 0.45,
  open_eye ≥ 0.55) gates what even gets sent from the MPU to the MCU; `sketch.ino` then
  applies its own second gate (`CONF_EYE_TH` = 0.20, `CONF_YAWN_TH` = 0.70) before
  counting a frame as closed/yawning. §5.1's single `EYE_CONF_MIN` = 0.50 describes
  neither layer.

None of this is a correction to the design below — the literature-backed D1/D2/D3 fusion
model remains the target if/when the canonical `dms-ap` pipeline (real face-landmark
model, not YOLOX class detection) is wired in, per `dms-ap-uno-q/README.md`'s own
integration note. This section exists so the gap between "what this document specifies"
and "what ships today" is not silently discovered by a judge or a future engineer.

---

## 1. Design philosophy

Three facts shape every number in this document.

**1. The three signals are not the same kind of evidence.** Distraction is a *behavioural* signal —
the driver is awake but not looking. Yawning is a *predictive* signal — it says drowsiness is
building, minutes before it becomes dangerous. Eye closure is an *imminent-danger* signal — it says
the driver is not conscious *right now*. Treating them with one shared threshold would either make
yawns alarming or make microsleeps late. So each domain has its own detector, its own timers, and
its own maximum severity it is allowed to reach on its own.

**2. Dwell time is the false-alarm defence, and it costs latency.** Every threshold below is a pair:
a condition, and how long it must hold. The dwell is what separates a blink from a microsleep and a
mirror check from inattention. It is also unavoidable delay added on top of pipeline latency. Both
numbers are stated separately everywhere, and neither is hidden inside the other.

**3. A false alarm is not free.** A driver who is woken by an alarm that was wrong learns to ignore
the alarm. The system that cries wolf at 2 a.m. is worse than no system, because the driver has
stopped believing it. This is why the L1 alert is a soft beep rather than a siren, why a single
yawn does nothing, and why the false-alarm rate is a hard requirement
([SYS-SR-008](01-system-requirements.md#7-safety-requirements)) rather than an aspiration.

---

## 2. Domain overview

| ID | Domain | Signal class | Time character | Max level reachable alone |
|---|---|---|---|---|
| **D1** | Distraction / eyes-off-road | Behavioural | Seconds | L2 |
| **D2** | Yawning | Predictive | Minutes | L2 |
| **D3** | Eye closure | Imminent danger | Sub-second to seconds | **L3** |

**DOM-000** — Only **D3** SHALL be capable of driving the system to L3 on its own evidence. D1 and
D2 SHALL be capped at L2 regardless of severity or duration.
*Rationale: L3 commands a vehicle to stop. The only observation that justifies "this driver is not
in control of the vehicle" is that their eyes have been shut for seconds. A driver who is looking
away, or yawning repeatedly, is still awake and still steering — bringing them to a halt on that
evidence is an intervention out of proportion to the observation, and it is exactly the behaviour
that gets a safety system switched off.*

Each domain produces a state from a common ladder:

```
IDLE ──▶ ACTIVE ──▶ SEVERE ──▶ CRITICAL   (CRITICAL: D3 only)
  ◀───────┴───────────┴───────────┘   via clear-dwell
```

---

## 3. D1 — Distraction (mất tập trung)

> **Not implemented in the deployed system** — see [§0](#0-implementation-status--what-is-actually-running-today-added-2026-08-16). The deployed model has no head-pose output.

### 3.1 What is measured

Head pose and gaze direction relative to a forward "road cone". The frame-level primitive is a
boolean **off-road**, computed per frame as:

```
off_road(frame) = (|yaw| > YAW_LIMIT)
               OR (pitch_down > PITCH_DOWN_LIMIT)
               OR (face present but both eye regions not visible)
```

Two derived metrics:

- **EOR** — *Eyes-Off-Road time*: the current uninterrupted run length of `off_road == true`.
- **EOR-cum** — cumulative off-road time within a rolling 12-second window.

### 3.2 Thresholds — **NORMATIVE**

| Parameter | Symbol | Value | Unit |
|---|---|---|---|
| Yaw limit (head turned left/right) | `YAW_LIMIT` | 30 | ° |
| Pitch-down limit (head/eyes down) | `PITCH_DOWN_LIMIT` | 20 | ° |
| Glance-noise floor (ignore shorter) | `GLANCE_MIN_MS` | 600 | ms |
| **D1 ACTIVE** — continuous EOR | `D1_ACTIVE_DWELL_MS` | **2000** | ms |
| **D1 SEVERE** — cumulative EOR in 12 s | `D1_SEVERE_CUM_MS` | **6000** | ms |
| Cumulative window | `D1_CUM_WINDOW_MS` | 12000 | ms |
| Clear dwell (on-road before de-escalating) | `D1_CLEAR_DWELL_MS` | 3000 | ms |
| Mirror-check suppression after indicator | `D1_INDICATOR_SUPPRESS_MS` | 3000 | ms |

### 3.3 Why these numbers

**The 2-second continuous threshold** is the most widely used single number in the distraction
literature and in regulation. The NHTSA Visual-Manual Driver Distraction Guidelines adopt a
2-second single-glance criterion for acceptable in-vehicle task design. Naturalistic driving data
(the VTTI 100-Car study) found that total eyes-off-road time exceeding roughly 2 seconds within a
6-second window is associated with an approximately **two-fold increase** in crash and near-crash
risk. Below 2 seconds, off-road glances are indistinguishable from the ordinary business of
driving — mirrors, instruments, blind-spot checks.

The physical statement is simple: at 90 km/h, **2 seconds is 50 metres travelled with no eyes on
the road.**

**The 600 ms noise floor** removes normal scanning. A mirror check is typically 300–500 ms; an
instrument-cluster glance is similar. Without this floor, an attentive driver doing exactly what
they were taught to do would accumulate D1 evidence continuously.

**The 6-seconds-in-12 cumulative rule** catches the pattern the continuous rule misses: the driver
who looks back at the road every 1.8 seconds while doing something else. Each individual glance is
"legal"; the aggregate is not. NHTSA's guidelines use a 12-second total-eyes-off-road budget for a
complete secondary task; sustaining half of a 12-second window off-road is far outside normal
driving.

**The 3-second clear dwell** is asymmetric on purpose: harder to leave a warning state than to
enter it. A driver who glances back at the road for 400 ms and away again has not returned their
attention to driving.

**The indicator suppression** encodes a piece of correct driving: before a lane change you *should*
turn your head. Penalising a shoulder check would train drivers out of a safe habit. (In the
demonstrator the indicator is a simulated input from the VCS.)

### 3.4 Behaviour — **NORMATIVE**

**DOM-D1-001** — A glance shorter than `GLANCE_MIN_MS` SHALL NOT contribute to EOR or EOR-cum.

**DOM-D1-002** — D1 SHALL enter ACTIVE when EOR ≥ `D1_ACTIVE_DWELL_MS`.

**DOM-D1-003** — D1 SHALL enter SEVERE when EOR-cum over the rolling 12 s window ≥
`D1_SEVERE_CUM_MS`, **or** when EOR ≥ 2 × `D1_ACTIVE_DWELL_MS` (4 s continuous).

**DOM-D1-004** — D1 SHALL return to IDLE only after `off_road == false` continuously for
`D1_CLEAR_DWELL_MS` **and** EOR-cum has fallen below `D1_SEVERE_CUM_MS`.

**DOM-D1-005** — While the simulated turn indicator is active and for
`D1_INDICATOR_SUPPRESS_MS` afterwards, yaw-based off-road detection **in the indicated direction**
SHALL be suppressed. Pitch-down detection SHALL NOT be suppressed.

**DOM-D1-006** — D1 SHALL NOT contribute above L2 in fusion (DOM-000).

### 3.5 Known limitation

D1 uses head pose as a proxy for gaze. A driver can keep their head forward and their eyes
elsewhere; D1 will not see it. Closing this gap requires per-driver gaze calibration, which is out
of scope for the competition build. **This limitation SHALL be stated whenever D1 performance is
reported** — reporting a distraction detection rate without it overstates the capability.

---

## 4. D2 — Yawning (ngáp)

### 4.1 What is measured

Mouth-open state per frame, from landmark-based mouth aperture estimation corroborated by a Mouth Aspect Ratio (MAR)
computed from mouth landmarks. A **yawn event** is a mouth-open episode long enough to be a yawn
rather than speech.

### 4.2 Thresholds — **NORMATIVE**

| Parameter | Symbol | Value | Unit |
|---|---|---|---|
| MAR threshold for "mouth open" | `MAR_OPEN` | 0.60 | — |
| Minimum episode length to count as a yawn | `YAWN_MIN_MS` | **1500** | ms |
| Maximum episode length still counted as one yawn | `YAWN_MAX_MS` | 12000 | ms |
| Counting window | `D2_WINDOW_MS` | **120000** | ms (2 min) |
| **D2 ACTIVE** — yawn count in window | `D2_ACTIVE_COUNT` | **2** | events |
| **D2 SEVERE** — yawn count in window | `D2_SEVERE_COUNT` | **3** | events |
| **D2 SEVERE** — alternative single-yawn duration | `D2_SEVERE_SINGLE_MS` | 5000 | ms |
| Clear: window must be empty for | `D2_CLEAR_MS` | 120000 | ms |

### 4.3 Why these numbers

**The 1.5-second minimum is the discriminator that makes D2 usable at all.** Everything else the
mouth does opens it: speaking, laughing, eating, singing, drinking. What separates those from a
yawn is duration. Conversational speech opens the mouth in bursts on the order of 150–250 ms
(syllable rates around 4–6 Hz); a genuine yawn is a slow, sustained aperture lasting several
seconds — commonly reported around 4–6 seconds. Setting the gate at 1.5 s sits an order of
magnitude above speech and comfortably below a real yawn. Without this gate, a driver on a
hands-free call would be flagged as drowsy within a minute.

**Two yawns in two minutes, not one.** A single yawn is a normal event with many causes — boredom,
a stuffy cabin, having just seen someone else yawn. Baseline yawn frequency in an alert person is
well under one per five minutes. Requiring **2 events inside 120 seconds** is roughly a
ten-fold elevation over baseline, which is a real signal, while a single yawn produces nothing at
all. This is a deliberate sensitivity sacrifice bought for credibility: the first time the system
beeps at someone for one yawn, it has lost that driver.

**Why yawning cannot reach L3.** Yawning is *predictive*. It indicates a driver who will be
dangerous in ten minutes, not one who is dangerous now. The correct response is a warning and a
suggestion to rest — never a vehicle intervention. It is the domain that should trigger *earliest*
and act *least*.

**The 12-second maximum** is a sanity gate: an episode longer than any plausible yawn indicates a
stuck detection or an occluded mouth, and SHALL be discarded rather than counted (and SHALL raise
`model_degraded`).

### 4.4 Behaviour — **NORMATIVE**

**DOM-D2-001** — A yawn event SHALL be registered at the *end* of a mouth-open episode whose
duration is in [`YAWN_MIN_MS`, `YAWN_MAX_MS`].

**DOM-D2-002** — An episode exceeding `YAWN_MAX_MS` SHALL be discarded and SHALL set the
`model_degraded` flag for the duration of the episode.

**DOM-D2-003** — D2 SHALL enter ACTIVE when the number of yawn events with timestamps inside the
rolling `D2_WINDOW_MS` reaches `D2_ACTIVE_COUNT`.

**DOM-D2-004** — D2 SHALL enter SEVERE when the count reaches `D2_SEVERE_COUNT`, **or** when the
count is ≥ `D2_ACTIVE_COUNT` and any event in the window had duration ≥ `D2_SEVERE_SINGLE_MS`.

**DOM-D2-005** — D2 SHALL return to IDLE when the rolling window contains fewer than
`D2_ACTIVE_COUNT` events.

**DOM-D2-006** — D2 SHALL NOT contribute above L2 in fusion (DOM-000).

**DOM-D2-007** — D2 state SHALL be preserved across an acknowledgement. Ack silences the alert;
it does not make the driver less tired.
*Rationale: if ack reset the yawn counter, a driver could dismiss their way through an entire
episode of building fatigue and the system would never escalate.*

---

## 5. D3 — Eye closure (nhắm mắt)

This is the domain that justifies the product. It has two independent sub-signals with completely
different time constants, and both are required.

### 5.1 Sub-signal (a) — Continuous closure / microsleep

**What is measured:** the current uninterrupted duration for which the eyes have been classified
closed.

#### Thresholds — **NORMATIVE**

| Parameter | Symbol | Value | Unit |
|---|---|---|---|
| **D3 ACTIVE** — continuous closure | `D3_ACTIVE_MS` | **800** | ms |
| **D3 SEVERE** — continuous closure (microsleep) | `D3_SEVERE_MS` | **1500** | ms |
| **D3 CRITICAL** — continuous closure (unresponsive) | `D3_CRITICAL_MS` | **3000** | ms |
| Closure clear dwell | `D3_CLEAR_MS` | 1000 | ms |
| Minimum eye-state confidence to count | `EYE_CONF_MIN` | 0.50 | — |

> **Deployed values differ** — see [§0](#0-implementation-status--what-is-actually-running-today-added-2026-08-16): shipped `sketch.ino` uses 1000/2000/4000 ms, not 800/1500/3000, and two confidence layers (0.35/0.45/0.55 in Python, 0.20/0.70 in the MCU) rather than one `EYE_CONF_MIN`.

#### Why these numbers

**800 ms — the blink boundary.** A spontaneous blink lasts roughly **100–400 ms** end to end. Even
the slow, heavy blinks characteristic of early fatigue stay near the top of that range. Setting the
first threshold at 800 ms places it at approximately **twice the upper bound of the normal blink
distribution**. This is the single most important design choice in the whole detector: it means a
D3 ACTIVE declaration cannot be caused by a normal blink, at any blink rate, for any driver. The
threshold does not need to be tuned per person because it is not near anyone's blink duration.

**1500 ms — the microsleep boundary.** Eyelid closure sustained past roughly one to two seconds is
the conventional automotive and clinical marker for a microsleep — a brief involuntary lapse of
consciousness. At 90 km/h, 1.5 seconds is **37.5 metres travelled with nobody driving.** This is
where the system stops warning and starts intervening.

**3000 ms — the unresponsive boundary.** Three seconds is where the pitch's headline number comes
from: at 90 km/h it is **100 metres**. No awake driver keeps their eyes shut for three continuous
seconds while operating a vehicle. Beyond this point the working assumption is that the driver is
not available to control the vehicle, and the vehicle acts.

The three numbers form a deliberate progression from "outside normal" (800) through "definitely
asleep" (1500) to "assume no driver" (3000), each roughly doubling the previous. The intervention
severity doubles with it.

#### What the driver actually experiences

Detection delay is *not* the threshold alone. It is:

```
time_to_alert = dwell + (1 / capture_fps) + pipeline_latency
```

At the specified 10 FPS and the 160 ms latency budget of
[01 §5](01-system-requirements.md#5-performance-requirements):

| Transition | Dwell | + frame quantisation | + pipeline | **Worst-case time to alert** |
|---|---:|---:|---:|---:|
| D3 ACTIVE (L1) | 800 ms | 100 ms | 160 ms | **≈ 1.06 s** |
| D3 SEVERE (L2) | 1500 ms | 100 ms | 160 ms | **≈ 1.76 s** |
| D3 CRITICAL (L3) | 3000 ms | 100 ms | 160 ms | **≈ 3.26 s** |

**These are the numbers that SHALL be quoted publicly**, not the bare dwell times. Quoting "we
detect a microsleep in 1.5 seconds" when the alert actually sounds at 1.76 s is the kind of small
dishonesty that a judge with a stopwatch will find.

### 5.2 Sub-signal (b) — PERCLOS trend

**What is measured:** **PERCLOS** = the proportion of time the eyes are at least 80 % closed
(the *P80* definition) over a rolling 60-second window. This is the most validated single
fatigue metric in the driver-monitoring literature and originates from the FHWA/Wierwille
driver-fatigue work.

#### Thresholds — **NORMATIVE**

| Parameter | Symbol | Value | Unit |
|---|---|---|---|
| Window length | `PERCLOS_WINDOW_MS` | **60000** | ms |
| Closure fraction defining "closed" | `PERCLOS_P` | 0.80 | — |
| Minimum valid frames to publish a value | `PERCLOS_MIN_SAMPLES` | 300 | frames |
| **D3 ACTIVE-TREND** | `PERCLOS_ACTIVE` | **0.08** | fraction |
| **D3 SEVERE-TREND** | `PERCLOS_SEVERE` | **0.15** | fraction |
| De-escalation margin (hysteresis) | `PERCLOS_HYST` | 0.02 | fraction |

#### Why these numbers

**Why a 60-second window.** PERCLOS is a statistic, and a statistic needs samples. Sixty seconds at
8–10 FPS gives 480–600 observations, enough for a stable estimate, while still being short enough
to track a driver's state as it changes over a drive. It is also the window length under which the
metric was validated, so the published threshold values apply.

**8 % and 15 %.** The literature consistently identifies the transition into meaningful drowsiness
in the region of roughly 8–15 % P80 PERCLOS, with values at and above ~15 % associated with
reliably degraded lane-keeping and reaction time. Below ~8 %, closure fraction is dominated by
ordinary blinking. We therefore use 8 % as "something is changing" and 15 % as "this driver is
measurably impaired".

**Why both sub-signals are needed.** They fail in opposite directions and cover each other:

| | Continuous closure (a) | PERCLOS (b) |
|---|---|---|
| Detects | A single dangerous event | A gradually deteriorating driver |
| Time to respond | Under 2 s | Up to 60 s |
| Misses | Slow degradation with no long closure | The first microsleep, entirely |
| False-positive source | A driver rubbing their eyes | High blink rate from dry air or screen glare |

A detector with only (a) is silent right up until the first microsleep. A detector with only (b)
cannot react in time to the microsleep when it comes. Together, (b) is the early warning and (a) is
the emergency.

### 5.3 Behaviour — **NORMATIVE**

**DOM-D3-001** — Continuous-closure timing SHALL only accumulate on frames where the eye-state
classification confidence ≥ `EYE_CONF_MIN`. Low-confidence frames SHALL **hold** the accumulator,
neither incrementing nor resetting it.
*Rationale: resetting on a low-confidence frame lets a single noisy frame cancel a genuine
microsleep. Incrementing on it lets a camera obstruction fabricate one. Holding does neither.*

**DOM-D3-002** — D3 SHALL enter ACTIVE at `D3_ACTIVE_MS`, SEVERE at `D3_SEVERE_MS`, CRITICAL at
`D3_CRITICAL_MS` of continuous closure.

**DOM-D3-003** — PERCLOS SHALL be published as INVALID (`0xFF` on CAN) until
`PERCLOS_MIN_SAMPLES` valid frames are in the window. An INVALID PERCLOS SHALL NOT contribute to
any state.

**DOM-D3-004** — PERCLOS crossing `PERCLOS_ACTIVE` SHALL raise D3 to at least ACTIVE; crossing
`PERCLOS_SEVERE` SHALL raise D3 to at least SEVERE. De-escalation SHALL require the value to fall
below the threshold by at least `PERCLOS_HYST`.

**DOM-D3-005** — The effective D3 state SHALL be the **maximum** of the state implied by sub-signal
(a) and the state implied by sub-signal (b).

**DOM-D3-006** — D3 CRITICAL SHALL be reachable **only** by sub-signal (a). PERCLOS SHALL NOT drive
CRITICAL at any value.
*Rationale: a high PERCLOS means the driver is impaired over the last minute. It does not mean they
are unconscious at this instant, and stopping the vehicle requires the instantaneous claim.*

**DOM-D3-007** — D3 CRITICAL SHALL NOT be clearable by driver acknowledgement
([SYS-FR-015](01-system-requirements.md#42-drowsiness-estimation)). It SHALL clear only when the
eyes are observed open with confidence ≥ `EYE_CONF_MIN` continuously for `D3_CLEAR_MS`.

**DOM-D3-008** — When the driver is detected as wearing dark sunglasses, or when eye-state
confidence is below `EYE_CONF_MIN` for more than 30 % of the last 3 seconds, D3 SHALL be marked
**UNAVAILABLE**, the `model_degraded` flag SHALL be set, and the system SHALL hold at L1 with a
distinct "eye tracking unavailable" indication. D3 SHALL NOT report IDLE in this condition.
*Rationale: "I cannot see the eyes" reported as "the eyes are open" is the most dangerous single
software defect this system can have.*

---

## 6. Fusion and the alert ladder

### 6.1 Level definitions — **NORMATIVE**

| Level | Name | Entry condition | Meaning |
|---|---|---|---|
| **L0** | NORMAL | All domains IDLE | Driver alert |
| **L1** | EARLY | Exactly one domain ACTIVE | Something is changing — inform, do not startle |
| **L2** | DROWSY | Any domain SEVERE, **OR** two or more domains ACTIVE simultaneously | Driver is impaired — intervene and reduce risk |
| **L3** | DANGER | **D3 CRITICAL**, **OR** L2 sustained > `L2_ESCALATE_MS` with no acknowledgement | Assume no driver — stop the vehicle |

| Parameter | Symbol | Value | Unit |
|---|---|---|---|
| L2 → L3 escalation without ack | `L2_ESCALATE_MS` | **10000** | ms |
| Level de-escalation dwell (all domains clear) | `LEVEL_CLEAR_MS` | **5000** | ms |
| Ack refractory (L1/L2 suppressed after ack) | `ACK_REFRACTORY_MS` | **60000** | ms |
| Maximum consecutive acks before refractory is disabled | `ACK_MAX_CONSECUTIVE` | **3** | — |

### 6.2 Why the multi-domain rule

**"Two ACTIVE domains = L2"** is the fusion rule that earns the three-domain architecture its
keep. A driver who is yawning *and* whose eyes are starting to close is in a qualitatively
different state from one doing either alone — and neither domain alone has crossed its SEVERE
threshold yet. Corroboration across independent evidence types is what allows each individual
threshold to stay conservative (low false-alarm rate) while the *system* stays sensitive.

### 6.3 Why L2 escalates to L3 after 10 seconds

If a driver is impaired enough to trigger L2, and ten seconds of alarm, vibration and cold air
produces no acknowledgement, the most likely explanation is that they are not able to respond. Ten
seconds is long enough that a merely-annoyed awake driver will have pressed the button, and short
enough that at 90 km/h it has not already been 250 metres.

### 6.4 Acknowledgement rules — **NORMATIVE**

**DOM-FUS-001** — Ack SHALL clear L1 and L2 to L0 and start `ACK_REFRACTORY_MS` during which
re-entry to L1 is suppressed. Re-entry to L2 SHALL NOT be suppressed.

**DOM-FUS-002** — Ack SHALL have no effect at L3 (DOM-D3-007).

**DOM-FUS-003** — After `ACK_MAX_CONSECUTIVE` acknowledgements within a single 10-minute period
with no intervening return to a sustained L0, the refractory SHALL be disabled and the system SHALL
indicate `ack_saturated`.
*Rationale: a driver repeatedly dismissing alerts is the exact population the device exists for. An
unlimited snooze button converts a safety device into a nuisance-silencer.*

**DOM-FUS-004** — Domain counters (notably D2's yawn window and D3's PERCLOS window) SHALL NOT be
reset by ack (DOM-D2-007).

### 6.5 Hysteresis — **NORMATIVE**

**DOM-FUS-005** — Escalation SHALL be immediate once a domain's dwell is satisfied.
De-escalation SHALL require **all** domains to be IDLE continuously for `LEVEL_CLEAR_MS`, and SHALL
step down **one level at a time**, not directly to L0.
*Rationale: asymmetry is the point. Being slow to relax and quick to warn is the correct bias for a
system whose failure mode is a crash.*

**DOM-FUS-006** — L3 SHALL NOT de-escalate automatically. Exit from L3 SHALL require an explicit
operator re-arm ([SYS-FR-033](01-system-requirements.md#44-vehicle-simulation)).

### 6.6 Worked timeline

A realistic escalation sequence, with what the driver observes at each point:

```
t=0:00   Driver yawns (2.1 s)                    → 1 yawn in window, D2 IDLE,  L0  (nothing)
t=0:47   Driver yawns (4.8 s)                    → 2 yawns in window, D2 ACTIVE, L1  soft beep, amber
t=1:30   PERCLOS reaches 0.09                    → D3 ACTIVE(trend). D2+D3 ACTIVE → L2
                                                    alarm + haptics + fan, red, speed cap 50 %
t=1:36   Driver presses "I am awake"             → L0, refractory 60 s. Yawn window NOT reset.
t=2:58   Eyes closed 0.9 s                       → D3 ACTIVE → L1 (refractory expired)  beep
t=3:04   Eyes closed 1.6 s                       → D3 SEVERE → L2  full alert, speed cap 50 %
t=3:14   No ack for 10 s at L2                   → L3  hazard + SOS + SAFE STOP
```

And the same driver, had the microsleep been longer instead:

```
t=3:04   Eyes closed 1.6 s                       → D3 SEVERE → L2
t=3:05.5 Eyes still closed, total 3.1 s          → D3 CRITICAL → L3 immediately, ack cannot clear
```

---

## 7. Fault and degraded states

These are **not** drowsiness states and SHALL be signalled distinguishably.

| State | Trigger | System response |
|---|---|---|
| `SENSOR_LOST` | No face detected for > 3000 ms while system armed | L1-equivalent indication + distinct flashing pattern; VCS applies the L1 speed cap; **no drowsiness alarm sound** |
| `MODEL_DEGRADED` | Eye confidence < `EYE_CONF_MIN` for > 30 % of last 3 s; or D2 episode > `YAWN_MAX_MS`; or sunglasses detected | D3 marked UNAVAILABLE (DOM-D3-008); hold at L1; indicate |
| `PIPELINE_SLOW` | Achieved FPS < 5 for > 5 s | Warn; extend all dwell accounting to real elapsed time (never frame counts); record in telemetry |
| `LINK_LOST` | See [05 §7](05-vehicle-control-spec.md#7-failsafe-behaviour) | VCS-side failsafe |

**DOM-FLT-001** — All dwell accumulation SHALL be computed from **frame timestamps**, never from
frame counts. A pipeline that drops to 3 FPS SHALL still declare an 800 ms closure at 800 ms of
wall-clock evidence, not after 8 frames.

**DOM-FLT-002** — A fault state SHALL NEVER be silently mapped onto L0. The permitted mappings are
fault → L1-equivalent or fault → higher. Never lower.

---

## 8. Threshold summary — the one table to check against code

**NORMATIVE.** CI asserts this table equals `config/thresholds.yaml`.

| Domain | Parameter | Value | Drives |
|---|---|---:|---|
| D1 | `YAW_LIMIT` | 30 ° | off_road |
| D1 | `PITCH_DOWN_LIMIT` | 20 ° | off_road |
| D1 | `GLANCE_MIN_MS` | 600 | noise floor |
| D1 | `D1_ACTIVE_DWELL_MS` | 2000 | ACTIVE |
| D1 | `D1_SEVERE_CUM_MS` / `D1_CUM_WINDOW_MS` | 6000 / 12000 | SEVERE |
| D1 | `D1_CLEAR_DWELL_MS` | 3000 | clear |
| D1 | `D1_INDICATOR_SUPPRESS_MS` | 3000 | suppression |
| D2 | `MAR_OPEN` | 0.60 | mouth open |
| D2 | `YAWN_MIN_MS` / `YAWN_MAX_MS` | 1500 / 12000 | event gate |
| D2 | `D2_WINDOW_MS` | 120000 | counting window |
| D2 | `D2_ACTIVE_COUNT` / `D2_SEVERE_COUNT` | 2 / 3 | ACTIVE / SEVERE |
| D2 | `D2_SEVERE_SINGLE_MS` | 5000 | SEVERE alt |
| D3a | `D3_ACTIVE_MS` | 800 | ACTIVE |
| D3a | `D3_SEVERE_MS` | 1500 | SEVERE |
| D3a | `D3_CRITICAL_MS` | 3000 | CRITICAL |
| D3a | `D3_CLEAR_MS` | 1000 | clear |
| D3a | `EYE_CONF_MIN` | 0.50 | validity |
| D3b | `PERCLOS_WINDOW_MS` | 60000 | window |
| D3b | `PERCLOS_P` | 0.80 | P80 definition |
| D3b | `PERCLOS_MIN_SAMPLES` | 300 | validity |
| D3b | `PERCLOS_ACTIVE` / `PERCLOS_SEVERE` | 0.08 / 0.15 | ACTIVE / SEVERE |
| D3b | `PERCLOS_HYST` | 0.02 | de-escalation |
| FUS | `L2_ESCALATE_MS` | 10000 | L2 → L3 |
| FUS | `LEVEL_CLEAR_MS` | 5000 | de-escalation |
| FUS | `ACK_REFRACTORY_MS` | 60000 | ack |
| FUS | `ACK_MAX_CONSECUTIVE` | 3 | ack saturation |
| FLT | `SENSOR_LOST_MS` | 3000 | fault |

---

## 9. Tuning protocol

**DOM-TUN-001** — All values above are **literature-derived priors**, not measurements from this
system. They SHALL be validated against the team's own annotated corpus
([06 §5](06-test-plan.md#5-test-corpora)) before the acceptance gate.

**DOM-TUN-002** — Tuning SHALL optimise for the operating point:
**maximise true-positive rate on the drowsy corpus subject to ≤ 1 false alarm per hour on the
baseline (alert-driver) corpus.** Reporting a TPR without the paired false-alarm rate is
meaningless and SHALL NOT be done.

**DOM-TUN-003** — `D3_ACTIVE_MS`, `D3_SEVERE_MS` and `D3_CRITICAL_MS` SHALL NOT be reduced below
the values in §8 without evidence that the measured blink-duration distribution for the test
population supports it. These three are the values a judge is most likely to challenge, and the
blink-physiology argument in §5.1 is what defends them.

**DOM-TUN-004** — Every tuning change follows the change-control process in
[02 §11](02-development-standards.md#11-change-control).

---

## 10. Design rationale references

The figures below informed the thresholds above. They are cited as the *provenance of the design
choice*, at the level of consensus values commonly reported in the driver-monitoring literature.
**Before any external publication these SHALL be re-verified against the primary sources and cited
precisely.**

| # | Basis | Used for |
|---|---|---|
| R1 | Wierwille et al., FHWA driver drowsiness research — origin of the P80 PERCLOS metric and its association with degraded driving performance | `PERCLOS_P`, `PERCLOS_WINDOW_MS`, `PERCLOS_ACTIVE`, `PERCLOS_SEVERE` |
| R2 | Klauer et al., VTTI 100-Car Naturalistic Driving Study — eyes-off-road time > ~2 s associated with ≈2× crash/near-crash risk | `D1_ACTIVE_DWELL_MS` |
| R3 | NHTSA Visual-Manual Driver Distraction Guidelines — 2 s single-glance and 12 s total-task-time criteria | `D1_ACTIVE_DWELL_MS`, `D1_CUM_WINDOW_MS`, `D1_SEVERE_CUM_MS` |
| R4 | Ophthalmological consensus on spontaneous blink duration, ~100–400 ms | `D3_ACTIVE_MS` (set at ≈2× the upper bound) |
| R5 | Automotive/clinical convention treating sustained eyelid closure ≳1–2 s as a microsleep | `D3_SEVERE_MS` |
| R6 | Reported yawn durations of ~4–6 s versus speech mouth-aperture episodes of ~150–250 ms | `YAWN_MIN_MS` |

**Open item ⚠️:** R1–R6 are recorded here as the reasoning that produced the numbers. The exact
citations have not yet been checked against the primary papers. This SHALL be closed before these
figures appear in any submitted document or public claim.

---

## Revision history

| Rev | Date | Author | Change |
|---|---|---|---|
| 0.1 | 2026-08-10 | ML_IoT_Love50 | Initial baseline |
| 0.1.1 | 2026-08-16 | ML_IoT_Love50 | Added §0 Implementation status documenting the gap between this spec and the deployed `MAIN_DMS_YOLOX_System` pipeline (no D1, no PERCLOS, no MAR, no fusion ladder, different D3a timing values, two-layer confidence gating). No normative values below §1 were changed. |
