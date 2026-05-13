# LFR Complete Package
## Line Following Robot — Full Firmware Suite
### STM32F103C8T6 (Blue Pill) · 14-Sensor Curved Wing Array

---

## ⚠ WHICH FOLDER TO USE

```
01_8IR_UPGRADED/   ← REFERENCE ONLY — built for 8-sensor Arduino (wrong hardware)
02_LFR_MASTER/     ← USE THIS — built for your actual 14-sensor STM32 hardware
```

`01_8IR_UPGRADED` was the first upgrade pass, written before the actual hardware
was identified. It is included for reference. Do not flash it to your robot.

---

## YOUR HARDWARE (confirmed during development)

| Component | Specification |
|---|---|
| MCU | STM32F103C8T6 (Blue Pill) |
| Sensor Array | 14-channel curved wing PCB (C0–C13) via CD74HC4067 MUX |
| ADC Resolution | 12-bit (0–4095) |
| Motor Driver | Custom RoboTech dual H-bridge (PWM + DIR type) |
| Motors | 2× brushed DC (2WD differential drive) |
| Display | SSD1306 128×64 OLED (I2C) |
| Buttons | 4× programmable + 1× power |
| Battery | 2S LiPo (7.4V nominal, 8.4V full, 6.6V cutoff) |

### Confirmed Pin Assignments (from hardware diagram)

| Pin | Function |
|---|---|
| PB15 | MUX S0 |
| PB14 | MUX S1 |
| PB13 | MUX S2 |
| PB12 | MUX S3 |
| PA0 | MUX Signal (ADC) |
| PA8 | Left Motor PWM (Timer1 CH1) |
| PA9 | Right Motor PWM (Timer1 CH2) |
| PB0 | Left Motor Direction ← **verify against your wiring** |
| PB1 | Right Motor Direction ← **verify against your wiring** |
| PA4 | Button UP |
| PA5 | Button DOWN |
| PA6 | Button SELECT |
| PA7 | Button BACK |
| PA1 | Battery voltage (via divider R1=100kΩ, R2=47kΩ) |
| PC13 | Onboard LED (active LOW) |

---

## 02_LFR_MASTER — File Guide

| File | Purpose | Key Contents |
|---|---|---|
| `config.h` | Single config source | All pins, defaults, EEPROM layout, enums, structs |
| `LFR_MASTER.ino` | Main file | All globals, setup(), loop() |
| `system.ino` | System management | CPU overclocking, battery monitoring, EEPROM, self-test, competition mode, run stats |
| `motor.ino` | Motor control | 20kHz hardware PWM, dead-band compensation, acceleration ramp, all motor tests |
| `sensor.ino` | Sensor reading | Atomic MUX select, 12-bit ADC oversampling, all sensor test screens |
| `calibration.ino` | Calibration | Auto sweep, manual, EEPROM 12-bit save/load, verify screen |
| `pid.ino` | PID engine | Full PID, 5 follow modes, look-ahead, crossing FSM, lost-line FSM, live tuning |
| `turns.ino` | Turn execution | Sensor-feedback turns, U-turn, junction detection, distance driving |
| `display.ino` | All OLED drawing | Every pixel drawn here — single source, no drawing in other files |
| `menu.ino` | Menu system | 10 top-level items, nested submenus, value editor, profile management |

---

## FIRST STEPS AFTER FLASHING

**Do these in order. Skipping any step will cause poor performance.**

### Step 1 — Dead Band Calibration
`Menu → Motor Test → Dead Band Cal`

Each physical motor starts moving at a different minimum PWM.
Without this, the robot drifts because one motor responds before the other.
The calibration finds that minimum for each motor and saves it.

### Step 2 — Sensor Calibration
`Menu → Calibration → Auto Calibrate`

Place the robot on your track. It will sweep left-right-right-left
while sampling all 14 sensors to find Min/Max ADC per channel.
Reference threshold = midpoint. Saved to EEPROM.

### Step 3 — Verify Calibration
`Menu → Calibration → Verify Calib`

Should show 14/14 OK. If any show `!!`, re-run Auto Calibrate.

### Step 4 — Check Sensor View
`Menu → Sensor Test → Digital View`

Place robot on the line. The active sensors should show filled bars.
Confirm sensors C5–C8 (center) activate when robot is centered on the line.

### Step 5 — PID Test Run (Safe Profile)
`Menu → Line Follow → Standard`

Robot uses Profile A (SAFE) by default: Kp=8, Kd=500, speed=150.
Press SELECT to stop. Check post-run stats screen for lost-line count.

### Step 6 — Tune PID
`Menu → PID Tuning`

Start with Kp. Increase until the robot oscillates, then back off 20%.
Then increase Kd until oscillation settles. Leave Ki=0 unless you see
consistent steady-state drift at curves.

### Step 7 — Competition Mode
`Menu → Competition`

Long-press SELECT to launch. Runs Profile A/B/C with countdown.
Auto-shows run stats after every run.

---

## PID TUNING GUIDE

### Understanding the Parameters

**Kp (Proportional)** — how hard to correct for current error.
Too low: robot wanders. Too high: robot oscillates side-to-side.
Start at 8, increase by 2 until oscillation starts, back off by 20%.

**Kd (Derivative)** — how hard to correct for rate of change.
Dampens oscillation from high Kp. Too high with low alpha: amplifies noise.
Start at 500. Increase until oscillation from Kp settles cleanly.

**Ki (Integral)** — corrects steady-state drift.
Start at 0. Only add if robot consistently hugs one side of a curve.
Even 0.1 has strong effect. Use integral_limit to prevent windup.

**Deriv Alpha** — low-pass filter on the derivative (0.1=smooth, 1.0=raw).
Lower = less noise, slower response. Higher = more noise, faster response.
Start at 0.4. Lower if motors jitter at speed.

**Base Speed** — starting point for both motors.
Increase after PID is stable. Higher speed = less time to correct = needs higher gains.

### Track-Specific Notes (your circuit schematic track)

| Track Feature | Recommended Setting |
|---|---|
| Dashed line section | `Drive Modes → Lost Line: Bridge, Bridge Hold: 80ms` |
| F-ONE / summing junction black boxes | `Drive Modes → Crossing Mode: Straight, Transit: 220ms` |
| Sharp letter corners (F, O, N, E) | Increase Kd, decrease Alpha to 0.3 |
| Inductor loops | Look-ahead enabled, Profile B gains |
| Diode triangle | Standard mode handles this — reduce base speed slightly |

---

## CPU OVERCLOCKING

Set in `Settings → CPU Speed` or `config.h → DEFAULT_CPU_SPEED_IDX`.

| Setting | Clock | Notes |
|---|---|---|
| 0 | 72 MHz | Stock spec, guaranteed stable |
| 1 | 96 MHz | Recommended minimum for competition |
| 2 | 128 MHz | Maximum, well-tested community OC, slight heat |

The Blue Pill is rated at 72MHz. 128MHz works on 99% of chips for short competition
runs. If random resets occur at 128MHz, drop to 96MHz.

---

## EEPROM MEMORY MAP

```
Address   Size    Content
0x000     2       Magic number (0xBE04 = valid data)
0x002     ~171    Settings struct (profiles A/B/C included)
0x100     28      Reference_ADC[14] — calibration midpoints
0x11C     28      Max_ADC[14] — calibration maximums
0x138     28      Min_ADC[14] — calibration minimums
0x154     —       End (340 bytes used / 1024 available)
```

**If you change the Settings struct layout**, increment `EEPROM_VERSION` in config.h.
This invalidates old EEPROM data and triggers a factory reset on next boot.

---

## WOKWI SIMULATION

To test menu/PID logic in Wokwi (wokwi.com, supports STM32F103):

Add to top of `system.ino`:
```cpp
#define WOKWI_SIM
#ifdef WOKWI_SIM
  void configureCPU(uint8_t) {}  // skip OC in simulation
#endif
```

Add to `motor.ino` around the Timer1 block:
```cpp
#ifndef WOKWI_SIM
  // ... TIM1 register code ...
#else
  pinMode(MOTOR_L_PWM, OUTPUT);
  pinMode(MOTOR_R_PWM, OUTPUT);
#endif
```

**What works in simulation:** Menu navigation, value editing, calibration flow,
PID math, OLED display, button logic.
**What does not:** Overclocking, Timer1 PWM, GPIO->ODR direct writes, ADC noise.

---

## KNOWN LIMITATIONS

1. No wheel encoders — distance driving (`driveDistance()`) is open-loop.
   Accuracy depends on consistent battery voltage and surface friction.

2. Battery monitoring requires a hardware voltage divider on PA1.
   Without it, `batteryMV` will read garbage. Connect or disable in `updateBattery()`.

3. OLED update during PID run is rate-limited to every 150ms.
   This is intentional — the OLED takes 6-10ms per frame and would slow the PID loop.

4. The crossing sequence array (`crossingSequence[]` in `LFR_MASTER.ino`) is
   hardcoded. Edit it directly for your specific track's junction decisions.

---

*LFR Master v3.0.0 · Built for RoboTech Innovator BD hardware · May 2026*
