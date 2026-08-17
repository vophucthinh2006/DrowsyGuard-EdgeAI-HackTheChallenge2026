/*
 * DrowsyGuard VCS — alert actuation implementation.
 *
 * Buzzer: PWM1 submodule 2 (PORT2_2 / PWM1_A2, Arduino J3-7). Frequency is
 * only reconfigured (PWM_SetupPwm, which resets the period) when it actually
 * changes -- toggling on/off at a fixed frequency is a cheap duty-cycle
 * update instead, since this runs every 10 ms tick.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "alerts.h"

#include "app.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "fsl_pwm.h"

#include "FreeRTOS.h"
#include "task.h"

typedef enum {
  kCondFault = 0, /* FAULT or ESTOP vehicle state -- highest priority */
  kCondLinkLost,
  kCondSensorLost,
  kCondL3,
  kCondL2,
  kCondL1,
  kCondL0,
} dg_alert_condition_t;

static dg_alert_condition_t s_prevCondition = kCondL0;
static uint32_t s_elapsedMs;

static uint32_t s_buzzerCurrentFreqHz;
static bool s_buzzerCurrentlyOn;

/* ---- buzzer (PWM1 SM2) ---------------------------------------------------- */

static void BuzzerInit(void) {
  pwm_config_t pwmConfig;
  PWM_GetDefaultConfig(&pwmConfig);
  pwmConfig.reloadLogic           = kPWM_ReloadPwmFullCycle;
  pwmConfig.pairOperation         = kPWM_Independent;
  pwmConfig.enableDebugMode       = true;
  /* Debug 2026-08-15: was `kPWM_Submodule0Clock` + `kPWM_Initialize_MasterSync`,
   * making this submodule (SM2) a *follower* that needs SM0 (motion.c's
   * left motor channel) to generate a working master sync pulse before SM2
   * ever runs -- meaning a buzzer-only test could never actually prove
   * "does PWM1 work" independent of the motor channels. Removed: leaving
   * clockSource/prescale/initializationControl at PWM_GetDefaultConfig()'s
   * plain defaults makes SM2 fully self-contained, same shape as how SM0
   * itself is configured in motion.c's InitPwmSubmodule(). If the buzzer
   * comes up on its own now, the master-sync dependency (or something
   * about SM0 specifically) was the real root cause all along. */
  (void)PWM_Init(BUZZER_PWM_BASEADDR, kPWM_Module_2, &pwmConfig);

  pwm_signal_param_t signal;
  signal.pwmChannel       = kPWM_PwmA;
  signal.level            = kPWM_HighTrue;
  signal.dutyCyclePercent = 0U;
  signal.deadtimeValue    = 0U;
  signal.faultState       = kPWM_PwmFaultState0;
  signal.pwmchannelenable = true;
  (void)PWM_SetupPwm(BUZZER_PWM_BASEADDR, kPWM_Module_2, &signal, 1U, kPWM_EdgeAligned,
                      2000U, BUZZER_PWM_SRC_CLK_FREQ);
  PWM_SetPwmLdok(BUZZER_PWM_BASEADDR, (uint8_t)kPWM_Control_Module_2, true);
  PWM_StartTimer(BUZZER_PWM_BASEADDR, (uint8_t)kPWM_Control_Module_2);

  s_buzzerCurrentFreqHz  = 2000U;
  s_buzzerCurrentlyOn    = false;
}

static void BuzzerSetOn(uint32_t freqHz) {
  if (freqHz != s_buzzerCurrentFreqHz) {
    pwm_signal_param_t signal;
    signal.pwmChannel       = kPWM_PwmA;
    signal.level            = kPWM_HighTrue;
    signal.dutyCyclePercent = 50U;
    signal.deadtimeValue    = 0U;
    signal.faultState       = kPWM_PwmFaultState0;
    signal.pwmchannelenable = true;
    (void)PWM_SetupPwm(BUZZER_PWM_BASEADDR, kPWM_Module_2, &signal, 1U, kPWM_EdgeAligned,
                        freqHz, BUZZER_PWM_SRC_CLK_FREQ);
    s_buzzerCurrentFreqHz = freqHz;
  } else if (!s_buzzerCurrentlyOn) {
    PWM_UpdatePwmDutycycle(BUZZER_PWM_BASEADDR, kPWM_Module_2, kPWM_PwmA, kPWM_EdgeAligned, 50U);
  }
  PWM_SetPwmLdok(BUZZER_PWM_BASEADDR, (uint8_t)kPWM_Control_Module_2, true);
  s_buzzerCurrentlyOn = true;
}

static void BuzzerSetOff(void) {
  if (s_buzzerCurrentlyOn) {
    PWM_UpdatePwmDutycycle(BUZZER_PWM_BASEADDR, kPWM_Module_2, kPWM_PwmA, kPWM_EdgeAligned, 0U);
    PWM_SetPwmLdok(BUZZER_PWM_BASEADDR, (uint8_t)kPWM_Control_Module_2, true);
    s_buzzerCurrentlyOn = false;
  }
}

/* ---- status LED (active-low) ---------------------------------------------- */

static void LedSet(bool red, bool green, bool blue) {
  GPIO_PinWrite(LED_RED_GPIO, LED_RED_PIN, red ? 0U : 1U);
  GPIO_PinWrite(LED_GREEN_GPIO, LED_GREEN_PIN, green ? 0U : 1U);
  GPIO_PinWrite(LED_BLUE_GPIO, LED_BLUE_PIN, blue ? 0U : 1U);
}

/* True on alternating half-periods of `periodMs` starting from elapsed=0. */
static bool Blink(uint32_t elapsedMs, uint32_t periodMs) {
  return (elapsedMs % periodMs) < (periodMs / 2U);
}

void Alerts_Init(void) {
  BuzzerInit();
  LedSet(false, true, false); /* boot: green, matches L0 until real data arrives */
}

static dg_alert_condition_t DeriveCondition(dg_vehicle_state_t vehState,
                                             dg_alert_level_t level, bool sensorLost,
                                             dg_link_state_t link,
                                             dg_fault_flags_t faults) {
  if (vehState == kDgVehFault || vehState == kDgVehEstop || faults.estop_active) {
    return kCondFault;
  }
  if (link == kDgLinkLost || vehState == kDgVehLinkLost) {
    return kCondLinkLost;
  }
  if (sensorLost) {
    return kCondSensorLost; /* VEH-032: distinct from every drowsiness level */
  }
  switch (level) {
    case kDgAlertL3Danger:
      return kCondL3;
    case kDgAlertL2Drowsy:
      return kCondL2;
    case kDgAlertL1Early:
      return kCondL1;
    case kDgAlertL0Normal:
    default:
      return kCondL0;
  }
}

void Alerts_Tick(uint32_t dt_ms, dg_vehicle_state_t vehState, dg_alert_level_t level,
                  bool sensorLost, dg_link_state_t link, dg_fault_flags_t faults) {
  dg_alert_condition_t cond = DeriveCondition(vehState, level, sensorLost, link, faults);

  if (cond != s_prevCondition) {
    s_elapsedMs     = 0U;
    s_prevCondition = cond;
  } else {
    s_elapsedMs += dt_ms;
  }

  switch (cond) {
    case kCondFault:
      BuzzerSetOn(1000U);
      LedSet(Blink(s_elapsedMs, 1000U), false, false);
      break;

    case kCondLinkLost:
      /* 50 ms on every 2000 ms (VEH-030). */
      if ((s_elapsedMs % 2000U) < 50U) {
        BuzzerSetOn(1000U);
      } else {
        BuzzerSetOff();
      }
      LedSet(false, false, false);
      /* amber = red+green together */
      GPIO_PinWrite(LED_RED_GPIO, LED_RED_PIN, Blink(s_elapsedMs, 500U) ? 0U : 1U);
      GPIO_PinWrite(LED_GREEN_GPIO, LED_GREEN_PIN, Blink(s_elapsedMs, 500U) ? 0U : 1U);
      break;

    case kCondSensorLost:
      BuzzerSetOff(); /* VEH-032: silent, on purpose */
      LedSet(false, false, Blink(s_elapsedMs, 1000U));
      break;

    case kCondL3:
      /* 3.2 kHz / 2.4 kHz alternating every 150 ms, continuous. */
      BuzzerSetOn(((s_elapsedMs % 300U) < 150U) ? 3200U : 2400U);
      LedSet(Blink(s_elapsedMs, 250U), false, false); /* ~4 Hz */
      break;

    case kCondL2:
      /* 200 ms on / 200 ms off, continuous. */
      if (Blink(s_elapsedMs, 400U)) {
        BuzzerSetOn(2800U);
      } else {
        BuzzerSetOff();
      }
      LedSet(true, false, false);
      break;

    case kCondL1: {
      /* Exactly 2 pulses of 100 ms on / 400 ms off, then silence until the
       * condition changes and comes back (VEH-031). */
      const uint32_t kPulsePeriodMs = 500U;
      uint32_t withinPulse           = s_elapsedMs % kPulsePeriodMs;
      uint32_t pulseIndex            = s_elapsedMs / kPulsePeriodMs;
      if (pulseIndex < 2U && withinPulse < 100U) {
        BuzzerSetOn(2000U);
      } else {
        BuzzerSetOff();
      }
      LedSet(true, true, false); /* amber = red+green */
      break;
    }

    case kCondL0:
    default:
      BuzzerSetOff();
      LedSet(false, true, false);
      break;
  }
}
