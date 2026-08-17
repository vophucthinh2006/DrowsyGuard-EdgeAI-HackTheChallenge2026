# Postmortem: PWM1 producing zero output on every channel (buzzer + motors)

**Status:** Fixed 2026-08-15. Verified via live register dump on real hardware
(FRDM-MCXN947), not yet re-confirmed audibly by ear after the fix landed.

## Symptom

- Software was 100% correct: `state=RUN`, `dutyL=47%(fwd)`, `dutyR=47%(fwd)`
  all computed and logged correctly.
- Motor terminals (M+/M-) measured 0 V. Buzzer (real hardware, confirmed
  connected) made no sound.
- Every single PWM1 channel was affected at once: buzzer (SM2) and all motor
  RPWM/LPWM channels (SM0/SM1/SM3).
- `OUTEN`, `MCTRL`, and the duty-cycle (`VAL2`/`VAL3`) registers all read back
  exactly as expected — nothing in the "obvious" registers indicated a
  problem.

## Dead ends ruled out (in order, each fixed but insufficient alone)

1. Missing `CLK3_EN` for PWM submodule 3 (`SYSCON->PWM1SUBCTL`) — real bug
   (comment said "3 submodules", code actually uses 4: SM0/1/2/3), fixed, but
   did not restore output on any channel.
2. Missing top-level `CLOCK_EnableClock(kCLOCK_Pwm1)` AHB gate — added,
   no change (later confirmed harmless/redundant: the SDK's own working
   reference example doesn't call this either, so it's likely enabled by
   default out of reset).
3. Missing `RESET_ClearPeripheralReset(kPWM1_RST_SHIFT_RSTn)` — added, no
   change (same as above, redundant with silicon reset defaults).
4. Buzzer submodule's `kPWM_Submodule0Clock` + `kPWM_Initialize_MasterSync`
   dependency on SM0 — removed so SM2 is fully self-contained, no change.
5. Wrong/zero PWM source clock frequency — `CLOCK_GetFreq(kCLOCK_BusClk)`
   confirmed 100 MHz via a diagnostic PRINTF in `motion.c`, a sane value.
   Ruled out.
6. Pin mux (ALT5 config on PORT2 pin 2 etc.) — confirmed byte-for-byte
   identical to the SDK's own working `driver_examples/pwm` reference
   example's `pin_mux.c`. Ruled out.
7. Boot clock function choice — `BOARD_BootClockPLL100M()` (ours, PLL1,
   needed for FlexCAN0's `kPLL1_CLK0_to_FLEXCAN0` routing) vs.
   `BOARD_InitBootClocks()`/`BOARD_BootClockPLL150M()` (SDK reference
   example, PLL0). Read and diffed both functions field-by-field: both
   ultimately do `CLOCK_AttachClk(kPLLx_to_MAIN_CLK)` +
   `CLOCK_SetClkDiv(kCLOCK_DivAhbClk, 1U)` — structurally equivalent for
   PWM1's purposes. Switching to the 150M/PLL0 path would have **broken
   FlexCAN0**, since `BOARD_BootClockPLL150M()` never configures PLL1 at
   all. Ruled out as the cause; correctly left alone.
8. Hardware/board/silicon — ruled out conclusively by building and flashing
   the SDK's own **unmodified** `driver_examples/pwm/pwm_3ph` reference
   example directly (bypassing this project's codebase entirely). It
   produced audible PWM output on the same physical buzzer. This proved the
   remaining bug had to be in this project's own source.
9. 2-phase bring-up test (`Alerts_RunPinBringupTest()` in `alerts.c`):
   phase 1 toggled the buzzer pin as plain GPIO (no PWM1 involved) — audible
   clicks, proving the pin/wiring/board path itself was fine. Phase 2
   switched the same pin back to PWM1_A2 — silent. This isolated the bug to
   PWM1's configuration specifically, not anything electrical.

## Root cause

A **live register dump** (added temporarily to `Alerts_RunPinBringupTest()`
in `alerts.c`, reading `PWM1`'s registers right after `BuzzerInit()` +
`BuzzerSetOn()`) showed:

- `PWM1->SM[2].DISMAP[0] == 0xFFFF` on power-up. This is the MCXN947's
  **silicon reset default**, not something our code set. Every one of PWM1's
  4 fault inputs is mapped, by default, to disable every channel (A/B/X) on
  every submodule.
- `PWM1->FSTS` bits 8–11 (the `FFPIN` field — the *live, unlatched* readback
  of the 4 physical fault input pins, as opposed to bits 0–3, `FFLAG`, the
  *latched* fault flag, which read 0) showed all 4 fault inputs reading
  logic-high.
- `PWM1->FCTRL2`'s `NOCOMB` bits default to 0, meaning the *combinational*
  fault path is enabled by default — the raw, live fault-pin state gates the
  PWM output in real time, with no latch involved at all.

Put together: PWM1's 4 fault inputs were floating/unconnected (this project
never wires anything to them — no fault sensors exist in this design), read
as logic-high, and — via the default DISMAP mapping and default combinational
path — continuously suppressed every PWM1 output on every submodule. This
happened entirely below the level of any register this project's code was
checking (`OUTEN`, `MCTRL`, duty registers) — those all reported "configured
and running" the whole time, because they were.

`FCTRL`/`FCTRL2`/`FSTS`/`DISMAP` fault-control registers are **PWM1-module-
wide** (shared across all 4 submodules), not per-submodule — this is exactly
why the buzzer (SM2) and every motor channel (SM0/SM1/SM3) were silent
simultaneously, with no single-channel exception.

The SDK's own `pwm_3ph` reference example only avoids this because it
explicitly calls `PWM_SetupFaults()` with `DEMO_PWM_FAULT_LEVEL = true`
(`examples/_boards/frdmmcxn947/driver_examples/pwm/cm33_core0/app.h`),
flipping the fault polarity so the same floating-high pin reading is
interpreted as "no fault." That's a board-specific tuning value, not a
general fix, and is fragile (depends on ambient float behavior staying
"safe" under that specific polarity choice).

## Fix

In `board_port/cm33_core0/hardware_init.c`, right after the PWM1 sub-clock
enables, explicitly clear the fault-disable mapping on every submodule this
project uses, since no hardware is wired to PWM1's fault inputs at all:

```c
PWM1->SM[0].DISMAP[0] = 0U;
PWM1->SM[1].DISMAP[0] = 0U;
PWM1->SM[2].DISMAP[0] = 0U;
PWM1->SM[3].DISMAP[0] = 0U;
```

This is deliberately simpler and more robust than replicating the
reference example's `PWM_SetupFaults()` + fault-polarity dance: with
`DISMAP` cleared, no fault source (real or floating) can ever gate PWM1's
outputs, regardless of `FSTS`/`FFPIN`/combinational-path state.

Note: register readback after the fix shows `DISMAP[0] == 0xF000`, not
`0x0000` — bits 12–15 are unimplemented/reserved on this register (only
bits 0–11 are defined: `DIS0A`/`DIS0B`/`DIS0X`) and appear to read back as
1 regardless of what's written. This is harmless; all *functional*
disable-mapping bits (0–11) read back as cleared.

## Verification

Rebuilt with `-Werror` (0 warnings), reflashed, captured two independent
serial register dumps after reset. Both show:

- `PWM1->OUTEN = 0xF10` — SM2's PWMA output enabled.
- `PWM1->MCTRL = 0xF00` — SM2's counter running.
- `PWM1->SM[2].VAL0 = 25000`, `VAL1 = 49999` — 50000-tick period (2 kHz at
  100 MHz bus clock), consistent with `BuzzerInit()`'s config.
- `PWM1->SM[2].VAL3 = 25000` — 50% duty cycle correctly programmed
  (previously 0 in earlier dumps, before this fix).
- `PWM1->SM[2].DISMAP[0]` functional bits (0–11) cleared.

All registers now read exactly as expected for a working PWM1 output.
**Audible confirmation from the user is the one remaining step** — not yet
done as of this writeup.

## Cleanup still pending (after audible confirmation)

See `WORKLOG_2026-08-15.md` for the full list of temporary test code added
during this investigation that needs to be removed/reverted once the fix is
confirmed working on real hardware (buzzer *and* motors):

- `Alerts_RunPinBringupTest()` and its call site in `main.c`'s `AlertTask`.
- The bisection debug `PRINTF`s and full register dump added to
  `alerts.c` for this investigation.
- `AlertTask`'s temporary halt-loop (restore the normal `Alerts_Tick()`
  loop, currently preserved as a comment block in `main.c`).
- `Alerts_ForceBuzzerOnForTest()` (unused, superseded by the 2-phase test).
- The instant-latching throttle behavior in `main.c`'s `ControlTask`
  (restore the ramped `GAS_RAMP_PCT_PER_S`/`BRAKE_RAMP_PCT_PER_S`
  hold-to-drive behavior).
