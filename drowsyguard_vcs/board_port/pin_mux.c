/*
 * Board-port pin muxing for drowsyguard_vcs (FRDM-MCXN947 / VCS node).
 *
 * Every pin below is cross-referenced against a real source rather than
 * guessed (contrast with wifi_sensing_npu's LPSPI6 ALT value, which is an
 * explicitly-flagged inference — nothing here carries that caveat):
 *
 *   - CAN0 TXD/RXD (PORT1_10/11, ALT11): the SDK's own
 *     examples/_boards/frdmmcxn947/driver_examples/flexcan/interrupt_transfer
 *     pin_mux.c configures the identical two pins the same way, and it
 *     matches the FRDM-MCXN947 schematic ("FlexCAN interface schematic",
 *     Table 14 "CAN header pinout": P1_10/CAN0_TXD, P1_11/CAN0_RXD feeding
 *     the on-board TJA1057GTK/3Z transceiver out to header J10). No external
 *     transceiver is needed — this closes specs/04 OI-04-01 for the VCS side.
 *   - PWM1_A0/A1/A2 (PORT2_6/4/2, ALT5): the SDK's own
 *     examples/_boards/frdmmcxn947/driver_examples/pwm pin_mux.c, which also
 *     documents them on Arduino header J3 (J3-15/11/7).
 *   - Status LED (PORT0_10 red / PORT0_27 green / PORT1_2 blue, ALT0,
 *     active-low): matches touch_rgb / wifi_sensing_npu on this same
 *     workspace, and independently confirmed against UM12018 Table 18 J2
 *     pin 4/6 ("RGB LED (P0_10/LED_RED)", "RGB LED (P0_27/LED_GREEN)") and
 *     Table 17 J1 pin 14 ("RGB LED (P1_2/LED_BLUE)").
 *   - Every other digital I/O pin (motor direction, STBY, buzzer... wait,
 *     buzzer is a PWM pin, see above) is a plain GPIO chosen from the
 *     Arduino-compatible header pins listed in UM12018 Tables 17-19 (J1/J2/
 *     J3) that carry no "Potential conflict" entry for this board's default
 *     resistor settings — see the per-signal comments below for the exact
 *     header pin.
 *
 * Full pin assignment table lives in ../README.md and mirrors
 * specs/05-vehicle-control-spec.md §2.2.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "pin_mux.h"

#include "fsl_common.h"
#include "fsl_gpio.h"
#include "fsl_port.h"

void BOARD_InitBootPins(void) { BOARD_InitPins(); }

/* Generic ALT0 GPIO electrical config, reused for every plain digital pin
 * below (motor direction/STBY, buzzer... buzzer is PWM, see PWM section;
 * vibration/fan/hazard outputs, ACK/re-arm/e-stop inputs). Slew/drive kept
 * at the same "fast / low" defaults the MCUXpresso Config Tool uses
 * elsewhere in this workspace (touch_rgb, wifi_sensing_npu) so nothing here
 * is a new, unreviewed electrical choice. */
static const port_pin_config_t kGpioOutConfig = {
    kPORT_PullDisable, kPORT_LowPullResistor, kPORT_FastSlewRate,
    kPORT_PassiveFilterDisable, kPORT_OpenDrainDisable, kPORT_LowDriveStrength,
    kPORT_MuxAlt0, kPORT_InputBufferEnable, kPORT_InputNormal, kPORT_UnlockRegister};

static const port_pin_config_t kGpioInPullUpConfig = {
    kPORT_PullUp, kPORT_LowPullResistor, kPORT_FastSlewRate,
    kPORT_PassiveFilterDisable, kPORT_OpenDrainDisable, kPORT_LowDriveStrength,
    kPORT_MuxAlt0, kPORT_InputBufferEnable, kPORT_InputNormal, kPORT_UnlockRegister};

/* E-stop sense is a normally-closed loop to ground on this design (see
 * README "E-stop wiring") — pulled up so an open loop (button pressed /
 * wire cut) reads high = asserted, and an intact loop reads low. Same
 * electrical shape as the ACK/re-arm buttons, kept as its own name so the
 * two are not accidentally merged if the wiring convention changes. */
#define kGpioEstopSenseConfig kGpioInPullUpConfig

static void InitGpioOutput(PORT_Type *port, GPIO_Type *gpio, uint32_t pin,
                            uint32_t initLogic) {
  PORT_SetPinConfig(port, pin, &kGpioOutConfig);
  gpio_pin_config_t cfg = {.pinDirection = kGPIO_DigitalOutput, .outputLogic = initLogic};
  GPIO_PinInit(gpio, pin, &cfg);
}

static void InitGpioInput(PORT_Type *port, GPIO_Type *gpio, uint32_t pin,
                           const port_pin_config_t *elec) {
  PORT_SetPinConfig(port, pin, elec);
  gpio_pin_config_t cfg = {.pinDirection = kGPIO_DigitalInput, .outputLogic = 0U};
  GPIO_PinInit(gpio, pin, &cfg);
}

void BOARD_InitPins(void) {
  CLOCK_EnableClock(kCLOCK_Port0);
  CLOCK_EnableClock(kCLOCK_Port1);
  CLOCK_EnableClock(kCLOCK_Port2);
  CLOCK_EnableClock(kCLOCK_Port4);
  CLOCK_EnableClock(kCLOCK_Gpio0);
  CLOCK_EnableClock(kCLOCK_Gpio1);
  CLOCK_EnableClock(kCLOCK_Gpio2);
  CLOCK_EnableClock(kCLOCK_Gpio4);

  /* ---- CAN0 (FLEXCAN0), on-board transceiver, header J10 ---------------- */
  const port_pin_config_t canPinConfig = {
      kPORT_PullDisable, kPORT_LowPullResistor, kPORT_FastSlewRate,
      kPORT_PassiveFilterDisable, kPORT_OpenDrainDisable, kPORT_LowDriveStrength,
      kPORT_MuxAlt11, kPORT_InputBufferEnable, kPORT_InputNormal, kPORT_UnlockRegister};
  PORT_SetPinConfig(PORT1, 10U, &canPinConfig); /* CAN0_TXD */
  PORT_SetPinConfig(PORT1, 11U, &canPinConfig); /* CAN0_RXD */

  /* ---- PWM1 (eFlexPWM), Arduino header J3 -------------------------------- */
  const port_pin_config_t pwmPinConfig = {
      kPORT_PullDisable, kPORT_LowPullResistor, kPORT_FastSlewRate,
      kPORT_PassiveFilterDisable, kPORT_OpenDrainDisable, kPORT_LowDriveStrength,
      kPORT_MuxAlt5, kPORT_InputBufferEnable, kPORT_InputNormal, kPORT_UnlockRegister};
  PORT_SetPinConfig(PORT2, 6U, &pwmPinConfig); /* PWM1_A0, J3-15 -> motor L speed */
  PORT_SetPinConfig(PORT2, 4U, &pwmPinConfig); /* PWM1_A1, J3-11 -> motor R speed */
  PORT_SetPinConfig(PORT2, 2U, &pwmPinConfig); /* PWM1_A2, J3-7  -> buzzer tone */
  /* PWM1_B0 (PORT2_7, J3-13) is deliberately left unconfigured: spare
   * hardware-PWM channel, e.g. for a future finer-grained vibration
   * intensity control. See README "Spare pins". */

  /* ---- Status LED (active-LOW), matches touch_rgb / wifi_sensing_npu ---- */
  InitGpioOutput(PORT0, GPIO0, 10U, 1U); /* red,   off (active-low) */
  InitGpioOutput(PORT0, GPIO0, 27U, 1U); /* green, off */
  InitGpioOutput(PORT1, GPIO1, 2U, 1U);  /* blue,  off */

  /* ---- Motor driver (TB6612FNG-style: PWM + 2 direction pins per channel,
   * one shared STBY) — see specs/05-vehicle-control-spec.md VEH-001/002.
   * Header pins per UM12018 Table 17/18, default resistor settings, no
   * listed conflict. ------------------------------------------------------ */
  InitGpioOutput(PORT0, GPIO0, 29U, 0U); /* AIN1, motor L dir bit 1 -- J1-D2 */
  InitGpioOutput(PORT1, GPIO1, 23U, 0U); /* AIN2, motor L dir bit 2 -- J1-D3 */
  InitGpioOutput(PORT0, GPIO0, 30U, 0U); /* BIN1, motor R dir bit 1 -- J1-D4 */
  InitGpioOutput(PORT0, GPIO0, 31U, 0U); /* BIN2, motor R dir bit 2 -- J1-D7 */
  InitGpioOutput(PORT0, GPIO0, 28U, 0U); /* STBY, shared driver enable, active-high -- J2-D8 */

  /* ---- Alert actuators (specs/05 §5) ------------------------------------ */
  InitGpioOutput(PORT0, GPIO0, 24U, 0U); /* vibration motor (through a low-side switch) -- J2-D11 */
  InitGpioOutput(PORT0, GPIO0, 26U, 0U); /* fan relay (through opto/relay module) -- J2-D12 */
  InitGpioOutput(PORT0, GPIO0, 25U, 0U); /* hazard lamp, left -- J2-D13 */
  InitGpioOutput(PORT4, GPIO4, 0U, 0U);  /* hazard lamp, right -- J2-D18 */

  /* ---- Driver / operator inputs ----------------------------------------- */
  InitGpioInput(PORT4, GPIO4, 1U, &kGpioInPullUpConfig); /* ACK "I am awake" -- J2-D19, active-low */
  InitGpioInput(PORT2, GPIO2, 3U, &kGpioInPullUpConfig); /* operator arm/re-arm -- J3-5, active-low */
  InitGpioInput(PORT2, GPIO2, 5U, &kGpioEstopSenseConfig); /* e-stop sense loop -- J3-9, see file header */
}
