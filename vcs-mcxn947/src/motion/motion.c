/*
 * DrowsyGuard VCS — motor drive implementation.
 *
 * Drives 2 BTS7960 modules (left/right channel), each through 2 independent
 * PWM1 channels — RPWM (forward) and LPWM (reverse) — plus one GPIO shared
 * across both modules' R_EN/L_EN enable inputs
 * (specs/05-vehicle-control-spec.md VEH-001/VEH-001a):
 *   forward, magnitude m: RPWM=m,   LPWM=0
 *   reverse, magnitude m: RPWM=0,   LPWM=m
 *   zero/hold:            RPWM=0,   LPWM=0   (active short-brake while enabled)
 *   disabled (coast):     shared enable GPIO low, regardless of RPWM/LPWM
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "motion.h"

#include <stdlib.h>

#include "app.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "fsl_pwm.h"

/* VEH-002: 20 kHz — below the BTS7960's ~25 kHz datasheet ceiling and above
 * the audible band. */
#define MOTION_PWM_FREQUENCY_HZ (20000UL)

/* VEH-021: ramp limiter, expressed per 10 ms control tick. */
#define MOTION_RAMP_PCT_PER_TICK (0.4f) /* 40 %/s * 0.010 s */

/* VEH-022: placeholder until OI-05-02 measures the real value on the loaded
 * chassis (static friction of the TT gear motors under this chassis' load
 * and battery state). 25 is the figure carried in the spec as a starting
 * point, not a measurement. */
#define MOTION_MIN_MOVE_DUTY (25U)

/* VEH-040 NORMATIVE */
#define MOTION_SAFE_STOP_RAMP_MS (1500U)
#define MOTION_SAFE_STOP_BRAKE_MS (500U)

/* Each channel drives one BTS7960 module through 2 independent PWM
 * channels — see the file header and specs/05 VEH-001a. RPWM/LPWM may live
 * on different submodules (the right channel's LPWM is borrowed from SM3,
 * see app.h) so each is named individually rather than assuming a shared
 * submodule. */
typedef struct {
  pwm_submodule_t rpwmModule;
  pwm_channels_t rpwmChannel;
  pwm_submodule_t lpwmModule;
  pwm_channels_t lpwmChannel;
  float currentSignedPct; /* ramp accumulator, signed: + forward, - reverse */
  float currentDutyPct;   /* |currentSignedPct|, kept for VCS_STATUS (duty_*_pct is unsigned) */
  int8_t currentSign;     /* -1, 0, +1, kept for VCS_STATUS (dir_*_reverse) -- reporting only,
                            * never fed back into the ramp (see TickChannel) */
} dg_channel_t;

static dg_channel_t s_left  = {kPWM_Module_0, kPWM_PwmA, kPWM_Module_0, kPWM_PwmB, 0.0f, 0.0f, 0};
static dg_channel_t s_right = {kPWM_Module_1, kPWM_PwmA, kPWM_Module_3, kPWM_PwmA, 0.0f, 0.0f, 0};

typedef enum { kSafeStopIdle, kSafeStopRamping, kSafeStopBraking, kSafeStopDone } dg_safe_stop_phase_t;

static dg_safe_stop_phase_t s_safeStopPhase = kSafeStopIdle;
static uint32_t s_safeStopPhaseElapsedMs;
static float s_safeStopRampStartPct;

static dg_motion_status_t s_lastStatus;

static uint8_t SpeedCapForState(dg_vehicle_state_t state, dg_link_state_t link) {
  /* VEH-020 NORMATIVE table. Fault/e-stop/init/disarmed/stopped/decel are
   * all "no commanded motion" states handled by the caller before this is
   * consulted (see Motion_Tick) — this function only answers "what's the
   * cap while actually driving". */
  if (link == kDgLinkLost) {
    return 30U; /* degraded/unknown beats any known drowsiness cap below L3 */
  }
  switch (state) {
    case kDgVehRun:
      return 100U;
    case kDgVehLimited:
      return 80U; /* refined further by alert level in control_task's
                     throttle shaping if needed; kept simple here since the
                     bring-up firmware has no live alert-level-to-cap split
                     beyond RUN/LIMITED yet -- see README open items. */
    default:
      return 0U;
  }
}

/* VEH-001a: RPWM=magnitude,LPWM=0 for forward; RPWM=0,LPWM=magnitude for
 * reverse; RPWM=LPWM=0 for zero/hold (an active short-brake while the
 * shared enable is asserted, not a coast — see the file header).
 *
 * ch->currentSign written here is a reporting artifact only (VCS_STATUS
 * dir_*_reverse) -- callers must never reconstruct a signed value from
 * ch->currentSign * ch->currentDutyPct, since any |signedPct| <= 0.5 collapses
 * currentSign to 0 here regardless of how much real ramp progress
 * currentDutyPct holds. TickChannel() keeps its own currentSignedPct
 * accumulator for exactly this reason. */
static void WriteChannelDuty(dg_channel_t *ch, float signedPct) {
  uint8_t rpwmDuty = 0U;
  uint8_t lpwmDuty = 0U;

  if (signedPct > 0.5f) {
    rpwmDuty        = (uint8_t)signedPct;
    ch->currentSign = 1;
  } else if (signedPct < -0.5f) {
    lpwmDuty        = (uint8_t)(-signedPct);
    ch->currentSign = -1;
  } else {
    ch->currentSign = 0;
  }

  PWM_UpdatePwmDutycycle(MOTION_PWM_BASEADDR, ch->rpwmModule, ch->rpwmChannel, kPWM_EdgeAligned,
                          rpwmDuty);
  PWM_UpdatePwmDutycycle(MOTION_PWM_BASEADDR, ch->lpwmModule, ch->lpwmChannel, kPWM_EdgeAligned,
                          lpwmDuty);
  PWM_SetPwmLdok(MOTION_PWM_BASEADDR,
                  (uint8_t)((1U << (uint8_t)ch->rpwmModule) | (1U << (uint8_t)ch->lpwmModule)),
                  true);
}

/* Configures 1 or 2 independent PWM channels (A only, or A+B) on one
 * submodule. `channels`/`channelCount` let the left channel (RPWM=A,
 * LPWM=B on the same submodule) and the right channel (RPWM=A on SM1,
 * LPWM=A on SM3 — see app.h) share one init routine. */
static void InitPwmSubmodule(pwm_submodule_t module, const pwm_channels_t *channels,
                              uint8_t channelCount) {
  pwm_config_t pwmConfig;
  PWM_GetDefaultConfig(&pwmConfig);
  pwmConfig.reloadLogic  = kPWM_ReloadPwmFullCycle;
  pwmConfig.pairOperation = kPWM_Independent;
  pwmConfig.enableDebugMode = true;
  if (module != kPWM_Module_0) {
    /* Share submodule 0's clock/sync so all channels stay frequency-locked
     * (matches the SDK's own pwm_3ph example pattern). */
    pwmConfig.clockSource           = kPWM_Submodule0Clock;
    pwmConfig.prescale              = kPWM_Prescale_Divide_1;
    pwmConfig.initializationControl = kPWM_Initialize_MasterSync;
  }

  if (PWM_Init(MOTION_PWM_BASEADDR, module, &pwmConfig) == kStatus_Fail) {
    PRINTF("[motion] PWM_Init failed for submodule %d\r\n", (int)module);
    return;
  }

  pwm_signal_param_t signals[2];
  for (uint8_t i = 0U; i < channelCount; i++) {
    signals[i].pwmChannel       = channels[i];
    signals[i].level            = kPWM_HighTrue;
    signals[i].dutyCyclePercent = 0U; /* motors start at zero duty, always */
    signals[i].deadtimeValue    = 0U;
    signals[i].faultState       = kPWM_PwmFaultState0;
    signals[i].pwmchannelenable = true;
  }

  (void)PWM_SetupPwm(MOTION_PWM_BASEADDR, module, signals, channelCount, kPWM_EdgeAligned,
                      (uint32_t)MOTION_PWM_FREQUENCY_HZ, MOTION_PWM_SRC_CLK_FREQ);
}

void Motion_Init(void) {
  const pwm_channels_t kLeftChannels[2]  = {kPWM_PwmA, kPWM_PwmB}; /* RPWM, LPWM */
  const pwm_channels_t kChannelAOnly[1]  = {kPWM_PwmA};

  InitPwmSubmodule(kPWM_Module_0, kLeftChannels, 2U); /* left RPWM+LPWM */
  InitPwmSubmodule(kPWM_Module_1, kChannelAOnly, 1U); /* right RPWM */
  InitPwmSubmodule(kPWM_Module_3, kChannelAOnly, 1U); /* right LPWM */

  PWM_SetPwmLdok(MOTION_PWM_BASEADDR,
                  kPWM_Control_Module_0 | kPWM_Control_Module_1 | kPWM_Control_Module_3, true);
  PWM_StartTimer(MOTION_PWM_BASEADDR,
                  kPWM_Control_Module_0 | kPWM_Control_Module_1 | kPWM_Control_Module_3);

  GPIO_PinWrite(MOTION_EN_GPIO, MOTION_EN_PIN, 0U); /* driver disabled at boot */

  /* DEV-033: debug_console_lite's PRINTF does not parse the "l" length
   * modifier -- %lu silently prints the literal characters "lu". Use %u
   * with an explicit cast instead, everywhere in this project. */
  PRINTF("[motion] PWM1 SM0/SM1/SM3 up at %u Hz, driver stage disabled\r\n",
         (unsigned int)MOTION_PWM_FREQUENCY_HZ);
}

/* VEH-022: motors don't turn below ~MOTION_MIN_MOVE_DUTY; map the linear
 * setpoint onto [MIN_MOVE_DUTY, 100] so "1 %" doesn't silently do nothing
 * and "50 %" doesn't feel like the low half of the real range. */
static float ApplyMinMoveDuty(float magnitude0to100) {
  if (magnitude0to100 <= 0.0f) {
    return 0.0f;
  }
  float mapped = (float)MOTION_MIN_MOVE_DUTY +
                 magnitude0to100 * (100.0f - (float)MOTION_MIN_MOVE_DUTY) / 100.0f;
  return mapped;
}

static float RampToward(float current, float target, float maxStep) {
  float delta = target - current;
  if (delta > maxStep) {
    delta = maxStep;
  } else if (delta < -maxStep) {
    delta = -maxStep;
  }
  return current + delta;
}

static void TickChannel(dg_channel_t *ch, int8_t setpointPct, uint8_t capPct) {
  float rawMagnitude = (float)abs((int)setpointPct);
  float capped       = rawMagnitude;
  if (capped > (float)capPct) {
    capped = (float)capPct;
  }
  float mapped = ApplyMinMoveDuty(capped);
  float signedTarget = (setpointPct < 0) ? -mapped : mapped;

  float next = RampToward(ch->currentSignedPct, signedTarget, MOTION_RAMP_PCT_PER_TICK);
  ch->currentSignedPct = next;
  ch->currentDutyPct   = (next < 0.0f) ? -next : next;
  WriteChannelDuty(ch, next);
}

static void RunSafeStop(uint32_t dt_ms) {
  s_safeStopPhaseElapsedMs += dt_ms;

  switch (s_safeStopPhase) {
    case kSafeStopIdle: {
      s_safeStopRampStartPct = (s_left.currentDutyPct > s_right.currentDutyPct)
                                    ? s_left.currentDutyPct
                                    : s_right.currentDutyPct;
      s_safeStopPhase          = kSafeStopRamping;
      s_safeStopPhaseElapsedMs = 0U;
      break;
    }
    case kSafeStopRamping: {
      /* VEH-041: linear in duty, symmetric on both channels (VEH-042). */
      float frac = (float)s_safeStopPhaseElapsedMs / (float)MOTION_SAFE_STOP_RAMP_MS;
      if (frac > 1.0f) {
        frac = 1.0f;
      }
      float target = s_safeStopRampStartPct * (1.0f - frac);
      float leftSigned  = (float)s_left.currentSign * target;
      float rightSigned = (float)s_right.currentSign * target;
      WriteChannelDuty(&s_left, leftSigned);
      WriteChannelDuty(&s_right, rightSigned);
      s_left.currentDutyPct    = target;
      s_right.currentDutyPct   = target;
      /* Keep the ramp accumulator in lockstep so that if the state leaves
       * DECEL early (re-armed mid-ramp) TickChannel() resumes from here,
       * not from a stale pre-DECEL value. */
      s_left.currentSignedPct  = leftSigned;
      s_right.currentSignedPct = rightSigned;

      if (s_safeStopPhaseElapsedMs >= MOTION_SAFE_STOP_RAMP_MS) {
        s_safeStopPhase          = kSafeStopBraking;
        s_safeStopPhaseElapsedMs = 0U;
      }
      break;
    }
    case kSafeStopBraking: {
      /* VEH-040 phase 2 / VEH-001a: RPWM=LPWM=0 with the shared enable
       * still asserted is the BTS7960 active short-brake state -- this is
       * "both H-bridge inputs asserted = motor short". */
      WriteChannelDuty(&s_left, 0.0f);
      WriteChannelDuty(&s_right, 0.0f);

      if (s_safeStopPhaseElapsedMs >= MOTION_SAFE_STOP_BRAKE_MS) {
        GPIO_PinWrite(MOTION_EN_GPIO, MOTION_EN_PIN, 0U); /* phase 3: disable (true coast) */
        s_left.currentSignedPct  = 0.0f;
        s_right.currentSignedPct = 0.0f;
        s_left.currentDutyPct    = 0.0f;
        s_right.currentDutyPct   = 0.0f;
        s_left.currentSign       = 0;
        s_right.currentSign      = 0;
        s_safeStopPhase        = kSafeStopDone;
        PRINTF("[motion] safe stop complete (motors disabled)\r\n");
      }
      break;
    }
    case kSafeStopDone:
    default:
      break; /* VEH-043: stays here until Motion_Tick sees a non-DECEL state */
  }
}

void Motion_Tick(uint32_t dt_ms, dg_vehicle_state_t state, dg_link_state_t link,
                  const dg_throttle_setpoint_t *setpoint) {
  if (state == kDgVehDecel) {
    GPIO_PinWrite(MOTION_EN_GPIO, MOTION_EN_PIN, 1U);
    RunSafeStop(dt_ms);
  } else {
    /* Leaving DECEL (either completed and re-armed, or never entered it):
     * reset the sequencer so the next L3 starts a fresh ramp. */
    s_safeStopPhase          = kSafeStopIdle;
    s_safeStopPhaseElapsedMs = 0U;

    if (state == kDgVehRun || state == kDgVehLimited) {
      GPIO_PinWrite(MOTION_EN_GPIO, MOTION_EN_PIN, 1U);
      uint8_t cap = SpeedCapForState(state, link);
      TickChannel(&s_left, setpoint->left_pct, cap);
      TickChannel(&s_right, setpoint->right_pct, cap);
    } else {
      /* VEH-010/VEH-056: every other state (INIT/DISARMED/ARMED_IDLE/
       * STOPPED/FAULT/ESTOP) drives with the stage fully disabled. */
      GPIO_PinWrite(MOTION_EN_GPIO, MOTION_EN_PIN, 0U);
      TickChannel(&s_left, 0, 0U);
      TickChannel(&s_right, 0, 0U);
    }
  }

  s_lastStatus.speed_cap_pct   = SpeedCapForState(state, link);
  s_lastStatus.duty_left_pct   = (uint8_t)s_left.currentDutyPct;
  s_lastStatus.dir_left_reverse  = (s_left.currentSign < 0);
  s_lastStatus.duty_right_pct  = (uint8_t)s_right.currentDutyPct;
  s_lastStatus.dir_right_reverse = (s_right.currentSign < 0);
}

bool Motion_SafeStopComplete(void) { return s_safeStopPhase == kSafeStopDone; }

dg_motion_status_t Motion_GetStatus(void) { return s_lastStatus; }
