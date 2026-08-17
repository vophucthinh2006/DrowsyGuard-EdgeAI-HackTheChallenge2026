/*
 * Board-port pin muxing for vcs-mcxn947 (FRDM-MCXN947 / VCS node).
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
 *   - PWM1_B0 (PORT2_7, ALT5, J3-13) and PWM1_A3 (PORT2_0, ALT5, J3-1): same
 *     ALT-column position (5) as the A0/A1/A2 pins above, cross-referenced
 *     against the SDK pin-signal tables for PORT2 (littlefs_shell / qdc
 *     examples' pin_mux.c, which list PIO2_7's and PIO2_0's ALT5 as
 *     PWM1_B0/PWM1_A3 respectively) and against project_template/pin_mux.c's
 *     `label: 'P2_0/J3[1]/...'` for the header position. Needed for the
 *     BTS7960 driver's RPWM/LPWM interface — see specs/05 VEH-001a.
 *   - Status LED (PORT0_10 red / PORT0_27 green / PORT1_2 blue, ALT0,
 *     active-low): matches touch_rgb / wifi_sensing_npu on this same
 *     workspace, and independently confirmed against UM12018 Table 18 J2
 *     pin 4/6 ("RGB LED (P0_10/LED_RED)", "RGB LED (P0_27/LED_GREEN)") and
 *     Table 17 J1 pin 14 ("RGB LED (P1_2/LED_BLUE)").
 *   - Every other digital I/O pin (driver enable, buzzer... wait, buzzer is
 *     a PWM pin, see above) is a plain GPIO chosen from the
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
 * below (driver enable, gas/brake inputs). Slew/drive kept at the same
 * "fast / low" defaults the MCUXpresso Config Tool uses elsewhere in this
 * workspace (touch_rgb, wifi_sensing_npu) so nothing here is a new,
 * unreviewed electrical choice. */
static const port_pin_config_t kGpioOutConfig = {
    kPORT_PullDisable, kPORT_LowPullResistor, kPORT_FastSlewRate,
    kPORT_PassiveFilterDisable, kPORT_OpenDrainDisable, kPORT_LowDriveStrength,
    kPORT_MuxAlt0, kPORT_InputBufferEnable, kPORT_InputNormal, kPORT_UnlockRegister};

static const port_pin_config_t kGpioInPullUpConfig = {
    kPORT_PullUp, kPORT_LowPullResistor, kPORT_FastSlewRate,
    kPORT_PassiveFilterDisable, kPORT_OpenDrainDisable, kPORT_LowDriveStrength,
    kPORT_MuxAlt0, kPORT_InputBufferEnable, kPORT_InputNormal, kPORT_UnlockRegister};

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
  /* PORT2 stays enabled for PWM1 pin muxing (motor + buzzer channels below)
   * even though nothing on PORT2 is read/written as plain GPIO anymore
   * (REARM_BUTTON/ESTOP_SENSE removed 2026-08-15) -- GPIO2's own peripheral
   * clock, only needed for GPIO_PinRead/Write on that port, is dropped.
   * Likewise PORT4/GPIO4 (ACK_BUTTON/HAZARD_R, also removed) is dropped
   * entirely -- nothing on this board uses PORT4 anymore. */
  CLOCK_EnableClock(kCLOCK_Port0);
  CLOCK_EnableClock(kCLOCK_Port1);
  CLOCK_EnableClock(kCLOCK_Port2);
  CLOCK_EnableClock(kCLOCK_Gpio0);
  CLOCK_EnableClock(kCLOCK_Gpio1);

  /* ---- Debug console (LPUART4 / FLEXCOMM4), MCU-Link virtual COM port ---
   * hardware_init.c clocks and attaches FLEXCOMM4 and calls
   * BOARD_InitDebugConsole(), but that only brings the *peripheral* up --
   * without this pin mux the TX/RX signals never reach the physical pins,
   * so PRINTF() runs, the UART shifts bits out internally, and nothing
   * ever appears on the serial console. Every other project pin_mux.c in
   * this workspace (touch_rgb, wifi_sensing_npu) configures this pair;
   * this file initially didn't, which is exactly the bug that made every
   * PRINTF() in src/ silently disappear until this fix. PORT1_8=FC4_P0
   * (TXD), PORT1_9=FC4_P1 (RXD), both kPORT_MuxAlt2. */
  const port_pin_config_t debugUartPinConfig = {
      kPORT_PullDisable, kPORT_LowPullResistor, kPORT_FastSlewRate,
      kPORT_PassiveFilterDisable, kPORT_OpenDrainDisable, kPORT_LowDriveStrength,
      kPORT_MuxAlt2, kPORT_InputBufferEnable, kPORT_InputNormal, kPORT_UnlockRegister};
  PORT_SetPinConfig(PORT1, 8U, &debugUartPinConfig); /* FC4_P0 / LPUART4 TXD */
  PORT_SetPinConfig(PORT1, 9U, &debugUartPinConfig); /* FC4_P1 / LPUART4 RXD */

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
  PORT_SetPinConfig(PORT2, 6U, &pwmPinConfig); /* PWM1_A0, J3-15 -> motor L RPWM */
  PORT_SetPinConfig(PORT2, 7U, &pwmPinConfig); /* PWM1_B0, J3-13 -> motor L LPWM */
  PORT_SetPinConfig(PORT2, 4U, &pwmPinConfig); /* PWM1_A1, J3-11 -> motor R RPWM */
  PORT_SetPinConfig(PORT2, 0U, &pwmPinConfig); /* PWM1_A3, J3-1  -> motor R LPWM (SM1's
                                                 * own B channel is not header-accessible
                                                 * on this board -- see app.h) */
  PORT_SetPinConfig(PORT2, 2U, &pwmPinConfig); /* PWM1_A2, J3-7  -> buzzer tone */

  /* ---- Status LED (active-LOW), matches touch_rgb / wifi_sensing_npu ---- */
  InitGpioOutput(PORT0, GPIO0, 10U, 1U); /* red,   off (active-low) */
  InitGpioOutput(PORT0, GPIO0, 27U, 1U); /* green, off */
  InitGpioOutput(PORT1, GPIO1, 2U, 1U);  /* blue,  off */

  /* ---- Motor driver (2x BTS7960: RPWM/LPWM per channel, one shared
   * R_EN+L_EN enable line across both modules) — see
   * specs/05-vehicle-control-spec.md VEH-001/VEH-001a. RPWM/LPWM pins are
   * configured above with the rest of PWM1; only the shared enable is a
   * plain GPIO. Header pin per UM12018 Table 18, no listed conflict. ------ */
  InitGpioOutput(PORT0, GPIO0, 28U, 0U); /* shared R_EN+L_EN, active-high -- J2-D8 */

  /* ---- Gas/brake pedal simulation -- onboard SW2/SW3, active-low -------- */
  InitGpioInput(PORT0, GPIO0, 23U, &kGpioInPullUpConfig); /* SW2 "Wakeup" button, reused as GAS */
  InitGpioInput(PORT0, GPIO0, 6U, &kGpioInPullUpConfig);  /* SW3 "ISP mode" button, reused as BRAKE */
}
