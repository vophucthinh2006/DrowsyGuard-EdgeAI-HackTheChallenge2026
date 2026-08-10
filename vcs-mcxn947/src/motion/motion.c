/*
 * DrowsyGuard VCS — motor drive implementation.
 *
 * Drives 2 independent PWM1 channels (submodule 0 = left, submodule 1 =
 * right) plus 2 direction GPIOs per channel in the TB6612FNG truth-table
 * convention (specs/05-vehicle-control-spec.md VEH-001):
 *   forward: AIN1=1,AIN2=0   reverse: AIN1=0,AIN2=1
 *   brake:   AIN1=1,AIN2=1   coast:   AIN1=0,AIN2=0
 * (BIN1/BIN2 for the right channel, same truth table.)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "motion.h"

#include <stdlib.h>

#include "app.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "fsl_pwm.h"

/* VEH-002: 20 kHz assumes the TB6612FNG (preferred) driver stage. If the
 * fallback L298N is used instead, this SHALL be reduced to 8 kHz and the
 * resulting audible whine recorded as a known deviation (see
 * specs/05-vehicle-control-spec.md VEH-002 and OI-05-03). */
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

typedef enum {
  kBrakeCoast = 0,
  kBrakeForward,
  kBrakeReverse,
  kBrakeShort,
} dg_bridge_mode_t;

typedef struct {
  pwm_submodule_t pwmModule;
  GPIO_Type *dirGpio1;
  uint32_t dirPin1;
  GPIO_Type *dirGpio2;
  uint32_t dirPin2;
  float currentDutyPct; /* signed magnitude after ramp, 0..100 */
  int8_t currentSign;   /* -1, 0, +1 */
} dg_channel_t;

static dg_channel_t s_left  = {kPWM_Module_0, MOTION_L_AIN1_GPIO, MOTION_L_AIN1_PIN,
                                MOTION_L_AIN2_GPIO, MOTION_L_AIN2_PIN, 0.0f, 0};
static dg_channel_t s_right = {kPWM_Module_1, MOTION_R_BIN1_GPIO, MOTION_R_BIN1_PIN,
                                MOTION_R_BIN2_GPIO, MOTION_R_BIN2_PIN, 0.0f, 0};

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

static void SetBridge(const dg_channel_t *ch, dg_bridge_mode_t mode) {
  switch (mode) {
    case kBrakeForward:
      GPIO_PinWrite(ch->dirGpio1, ch->dirPin1, 1U);
      GPIO_PinWrite(ch->dirGpio2, ch->dirPin2, 0U);
      break;
    case kBrakeReverse:
      GPIO_PinWrite(ch->dirGpio1, ch->dirPin1, 0U);
      GPIO_PinWrite(ch->dirGpio2, ch->dirPin2, 1U);
      break;
    case kBrakeShort:
      GPIO_PinWrite(ch->dirGpio1, ch->dirPin1, 1U);
      GPIO_PinWrite(ch->dirGpio2, ch->dirPin2, 1U);
      break;
    case kBrakeCoast:
    default:
      GPIO_PinWrite(ch->dirGpio1, ch->dirPin1, 0U);
      GPIO_PinWrite(ch->dirGpio2, ch->dirPin2, 0U);
      break;
  }
}

static void WriteChannelDuty(dg_channel_t *ch, float signedPct) {
  dg_bridge_mode_t mode;
  uint8_t magnitude;

  if (signedPct > 0.5f) {
    mode      = kBrakeForward;
    magnitude = (uint8_t)signedPct;
    ch->currentSign = 1;
  } else if (signedPct < -0.5f) {
    mode      = kBrakeReverse;
    magnitude = (uint8_t)(-signedPct);
    ch->currentSign = -1;
  } else {
    mode      = kBrakeCoast;
    magnitude = 0U;
    ch->currentSign = 0;
  }

  SetBridge(ch, mode);
  PWM_UpdatePwmDutycycle(MOTION_PWM_BASEADDR, ch->pwmModule, kPWM_PwmA, kPWM_EdgeAligned,
                          magnitude);
  PWM_SetPwmLdok(MOTION_PWM_BASEADDR, (uint8_t)(1U << (uint8_t)ch->pwmModule), true);
}

static void InitPwmChannel(pwm_submodule_t module) {
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

  pwm_signal_param_t signal;
  signal.pwmChannel       = kPWM_PwmA;
  signal.level            = kPWM_HighTrue;
  signal.dutyCyclePercent = 0U; /* motors start at zero duty, always */
  signal.deadtimeValue    = 0U;
  signal.faultState       = kPWM_PwmFaultState0;
  signal.pwmchannelenable = true;

  (void)PWM_SetupPwm(MOTION_PWM_BASEADDR, module, &signal, 1U, kPWM_EdgeAligned,
                      (uint32_t)MOTION_PWM_FREQUENCY_HZ, MOTION_PWM_SRC_CLK_FREQ);
}

void Motion_Init(void) {
  InitPwmChannel(kPWM_Module_0); /* left */
  InitPwmChannel(kPWM_Module_1); /* right */

  PWM_SetPwmLdok(MOTION_PWM_BASEADDR, kPWM_Control_Module_0 | kPWM_Control_Module_1, true);
  PWM_StartTimer(MOTION_PWM_BASEADDR, kPWM_Control_Module_0 | kPWM_Control_Module_1);

  GPIO_PinWrite(MOTION_STBY_GPIO, MOTION_STBY_PIN, 0U); /* driver disabled at boot */
  SetBridge(&s_left, kBrakeCoast);
  SetBridge(&s_right, kBrakeCoast);

  /* DEV-033: debug_console_lite's PRINTF does not parse the "l" length
   * modifier -- %lu silently prints the literal characters "lu". Use %u
   * with an explicit cast instead, everywhere in this project. */
  PRINTF("[motion] PWM1 SM0/SM1 up at %u Hz, driver stage disabled\r\n",
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

  float signedCurrent = (float)ch->currentSign * ch->currentDutyPct;
  float next = RampToward(signedCurrent, signedTarget, MOTION_RAMP_PCT_PER_TICK);

  ch->currentDutyPct = (next < 0.0f) ? -next : next;
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
      s_left.currentDutyPct  = target;
      s_right.currentDutyPct = target;

      if (s_safeStopPhaseElapsedMs >= MOTION_SAFE_STOP_RAMP_MS) {
        s_safeStopPhase          = kSafeStopBraking;
        s_safeStopPhaseElapsedMs = 0U;
      }
      break;
    }
    case kSafeStopBraking: {
      SetBridge(&s_left, kBrakeShort);
      SetBridge(&s_right, kBrakeShort);
      PWM_UpdatePwmDutycycle(MOTION_PWM_BASEADDR, kPWM_Module_0, kPWM_PwmA, kPWM_EdgeAligned, 0U);
      PWM_UpdatePwmDutycycle(MOTION_PWM_BASEADDR, kPWM_Module_1, kPWM_PwmA, kPWM_EdgeAligned, 0U);
      PWM_SetPwmLdok(MOTION_PWM_BASEADDR, kPWM_Control_Module_0 | kPWM_Control_Module_1, true);

      if (s_safeStopPhaseElapsedMs >= MOTION_SAFE_STOP_BRAKE_MS) {
        GPIO_PinWrite(MOTION_STBY_GPIO, MOTION_STBY_PIN, 0U); /* phase 3: disable */
        SetBridge(&s_left, kBrakeCoast);
        SetBridge(&s_right, kBrakeCoast);
        s_left.currentDutyPct  = 0.0f;
        s_right.currentDutyPct = 0.0f;
        s_left.currentSign     = 0;
        s_right.currentSign    = 0;
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
    GPIO_PinWrite(MOTION_STBY_GPIO, MOTION_STBY_PIN, 1U);
    RunSafeStop(dt_ms);
  } else {
    /* Leaving DECEL (either completed and re-armed, or never entered it):
     * reset the sequencer so the next L3 starts a fresh ramp. */
    s_safeStopPhase          = kSafeStopIdle;
    s_safeStopPhaseElapsedMs = 0U;

    if (state == kDgVehRun || state == kDgVehLimited) {
      GPIO_PinWrite(MOTION_STBY_GPIO, MOTION_STBY_PIN, 1U);
      uint8_t cap = SpeedCapForState(state, link);
      TickChannel(&s_left, setpoint->left_pct, cap);
      TickChannel(&s_right, setpoint->right_pct, cap);
    } else {
      /* VEH-010/VEH-056: every other state (INIT/DISARMED/ARMED_IDLE/
       * STOPPED/FAULT/ESTOP) drives with the stage fully disabled. */
      GPIO_PinWrite(MOTION_STBY_GPIO, MOTION_STBY_PIN, 0U);
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
