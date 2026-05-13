# Conversation Summary & Development Log
## Line Following Robot — Complete Development Record

---

## PHASE 1 — Initial Code Upgrade (8-Sensor Arduino)

### What You Provided
- A ZIP containing 8 `.ino` files for an existing line following robot
- Original code used: 8 IR sensors through a CD74HC4067 MUX, Arduino framework,
  U8glib OLED display, 3-button menu, basic PD controller

### Bugs Found in Original Code

| Bug | Location | Impact |
|---|---|---|
| `pinMode(left_motor_speed)` written twice, right motor PWM pin never initialized | `setup()` | Right motor PWM unreliable |
| `String direction` variable using heap allocation | globals | Memory fragmentation over time |
| EEPROM stored ADC values divided by 4 | `SensorCalibration.ino` | Lost 75% of ADC precision |
| `LoadCalibration()` only printed labels, not values | `SensorCalibration.ino` | Debug output useless |
| `kp` and `kd` as `int` instead of `float` | globals | Coarse-grained PID tuning |
| No integral term | `PID_Controller.ino` | No steady-state correction |
| No output clamping on PID | `PID_Controller.ino` | Integer overflow possible |
| No lost-line handling | `PID_Controller.ino` | Robot runs blind when line lost |
| No exit from PID loop | `PID_Controller.ino` | Required hardware reset to stop |
| Turn functions had infinite loops, no timeout | `Turns.ino` | Infinite spin if line lost during turn |
| `motor(0,0)` set backward direction pins | `motor.ino` | Wrong coast behavior |
| `Serial.begin(9600)` | `setup()` | Extremely slow debug output |
| Menu action triggered AND screen cycled same press | `menu.ino` | Inconsistent menu state |

### What Was Added in 8IR_UPGRADED
- Full PID (P + I with anti-windup + filtered D)
- Derivative low-pass filter (alpha = 0.4)
- Lost-line recovery (bridge → spin → timeout)
- SELECT button exits PID back to menu
- ADC oversampling (3 samples per channel)
- MUX settling delay reduced 20µs → 10µs
- EEPROM full 10-bit precision (2 bytes per value)
- Turn timeout protection
- Serial at 115200 baud
- String → enum for direction flag
- Fixed all 13 bugs listed above

---

## PHASE 2 — Hardware Identification

### Track Analysis
You showed a competition track made entirely from **electrical circuit schematic symbols**:
- Dashed/dotted wire sections (gaps in the line)
- Summing junction block (⊕) — wide black rectangle
- Resistor, diode, inductor symbols
- F-ONE text block — hard 90° corners
- Multiple T-junctions and crossings

### Key Challenges Identified
| Track Feature | Challenge |
|---|---|
| Dashed lines | Robot loses line at every gap |
| Wide black blocks | All sensors activate, PID loses reference |
| Sharp corners (F, O, N, E) | Fast overshoot causes exit |
| Inductor loops | Tight radius requires early correction |
| T-junctions | Must decide which direction to take |

### Actual Hardware Revealed
You then shared the real hardware, which was completely different from what the
8IR_UPGRADED code was written for:

| Parameter | 8IR_UPGRADED assumed | Actual hardware |
|---|---|---|
| MCU | Arduino (AVR) | STM32F103C8T6 (Blue Pill) |
| Sensors | 8, straight array | 14, curved wing array (C0–C13) |
| ADC | 10-bit (0–1023) | 12-bit (0–4095) |
| Sensor PCB | Flat | Curved/angled wings |
| MUX pins | D8, D10, D11, D12, A7 | PB15, PB14, PB13, PB12, PA0 |

**Conclusion:** 8IR_UPGRADED was thrown away. A complete rewrite was required.

### Why the 14-Sensor Curved Wing Is an Advantage
- **Dashed lines:** Wide array spans multiple dashes simultaneously — may bridge gaps naturally
- **Intersections:** 14 sensors produce distinctive "all active" pattern at junctions
- **Curves:** Outer angled sensors (C0–C2, C11–C13) see the line curving before the
  center sensors do — enables look-ahead correction
- **Sharp corners:** Wide coverage means the line is never completely lost at corners

---

## PHASE 3 — Full Menu Design

### Button Layout (4 programmable buttons confirmed)
| Button | In Menu | While PID Running |
|---|---|---|
| UP | Navigate up / increment value | Speed +5 live |
| DOWN | Navigate down / decrement value | Speed -5 live |
| SELECT | Enter / confirm / save | Stop → post-run stats → menu |
| BACK | Go back one level / cancel edit | Toggle turbo mode |

### Complete Menu Tree Designed
```
ROOT (10 items)
├── 1. Line Follow
│     ├── Standard (PID center follow)
│     ├── Center Mode (non-linear aggressive snap)
│     ├── Left Edge (follows left edge of line)
│     ├── Right Edge (follows right edge of line)
│     └── Turbo Mode (× turbo_mult speed boost)
├── 2. Calibration
│     ├── Auto Calibrate (sweep L/R/R/L)
│     ├── Manual Calibrate (white snapshot → black snapshot)
│     ├── View Values (scrollable min/ref/max all 14 sensors)
│     ├── Verify Calibration (PASS/FAIL per channel)
│     └── Reset Calibration (wipes EEPROM block)
├── 3. PID Tuning
│     ├── Kp, Ki, Kd (float editors, hold=fast scroll)
│     ├── Base Speed, Max Speed
│     ├── Turbo Multiplier
│     ├── Deriv Alpha (LPF coefficient)
│     ├── Integral Limit (anti-windup clamp)
│     ├── Accel Ramp (ms)
│     ├── Save to EEPROM
│     └── Load EEPROM
├── 4. Drive Modes
│     ├── Left/Right Edge Offsets
│     ├── Lost Line Mode (Bridge/Spin/Stop)
│     ├── Bridge Hold Time (ms)
│     ├── Spin Timeout (ms)
│     ├── Crossing Mode (Straight/Sequence/Left/Right)
│     ├── Crossing Transit Time
│     └── Look-ahead toggle
├── 5. Sensor Test
│     ├── Analog View (live 12-bit values)
│     ├── Digital View (0/1 bar per sensor)
│     ├── Position View (position gauge + error)
│     ├── Noise Test (1000 samples, shows variance)
│     └── Sensor Health (PASS/FAIL vs calibration)
├── 6. Motor Test
│     ├── Forward, Reverse
│     ├── Left Motor Only, Right Motor Only
│     ├── Spin Left, Spin Right
│     ├── PWM Sweep (0→255→0)
│     └── Dead Band Calibration
├── 7. Settings
│     ├── CPU Speed (72/96/128 MHz)
│     ├── Line Mode (black/white or white/black)
│     ├── ADC Resolution (8/10/12 bit)
│     ├── Serial Baud Rate
│     ├── Telemetry Mode (Off/Error/Full PID stream)
│     ├── OLED During Run (On/Dim/Off)
│     ├── Start Delay (0/3/5/10 seconds)
│     ├── Battery Cutoff (mV)
│     ├── Dead Band L/R
│     └── Factory Reset
├── 8. Competition Mode
│     ├── Profile selector
│     ├── Start delay
│     ├── OLED mode
│     └── LAUNCH (long SELECT → countdown → go)
├── 9. Profiles (A=SAFE, B=NORMAL, C=TURBO)
│     ├── Select and edit each profile
│     └── Copy A→B, B→C, C→A
└── 10. System Info
      ├── Firmware version, date
      ├── CPU speed (live, confirms OC)
      ├── Battery voltage
      └── EEPROM usage
```

---

## PHASE 4 — LFR_MASTER Build

### Architecture Decisions

**Why direct register writes instead of digitalWrite/analogWrite:**
`GPIOB->ODR` atomic 4-bit write selects all 4 MUX pins simultaneously in one
CPU instruction. 4× faster than 4 separate `digitalWrite()` calls. Critical in the
hot path called 14 times per sensor scan.

**Why 20kHz hardware PWM instead of Arduino analogWrite default (1kHz):**
20kHz is above human hearing range — no motor whine. Better efficiency.
Better torque at low duty cycles. Uses Timer1 CH1/CH2 on PA8/PA9 directly.

**Why derivative low-pass filter:**
The MUX/ADC chain introduces electrical noise, especially with motors running.
Without filtering, large kd values amplify every noise spike into a violent motor
correction. Alpha=0.4 balances noise rejection vs response speed.

**Why 3 PID profiles:**
Each track run requires different gains. Profile A (SAFE) for calibration and
learning. Profile B (NORMAL) for main runs. Profile C (TURBO) for final timed runs
when you know the track. Switching profiles is one button press in Competition Mode.

**Why look-ahead multipliers on outer sensors:**
C0–C2 and C11–C13 are physically angled outward on the curved wing PCB.
They physically "see" the track ahead of the robot's centerline. When they
activate, a sharp curve is approaching. Multiplying their contribution to the
PID correction gives earlier, stronger response at curves. Multipliers:
C0/C13 = 2.2×, C1/C12 = 1.8×, C2/C11 = 1.4×, center = 1.0×.

**Why separate turns.ino instead of inline in pid.ino:**
The crossing handler in pid.ino originally had inline turn logic. turns.ino provides:
sensor-feedback turns (wait for specific sensor to activate, not timed guesses),
U-turn, junction type detection, timed distance driving, and the dashed line bridge.
Single source of truth for all movement decisions at junctions.

**Why display.ino as single drawing file:**
All OLED drawing originally scattered across pid.ino, sensor.ino, system.ino.
Centralizing in display.ino means UI changes require editing one file, and
eliminates duplicate function definitions that caused compile errors.

### Overclocking Implementation
STM32F103 spec: 72MHz max.
Implementation: Direct RCC register manipulation — sets PLL multiplier to ×16
from 8MHz HSE crystal = 128MHz. Also sets flash wait states (2), APB1 prescaler
(÷4 = 32MHz, under 36MHz spec), reconfigures SysTick for correct millis() timing.
Called at the very start of setup() before any timing-dependent initialization.

### Lost-Line State Machine (3 phases)
1. **Bridge (0 → bridge_hold_ms):** Hold exact last motor PWM output.
   The robot coasts through gaps using momentum. Ideal for dashed lines.
2. **Spin (bridge_hold_ms → bridge_hold_ms + spin_timeout_ms):** Rotate
   toward last known error direction at 45% speed. Finds line after sharp curves.
3. **Give up (after spin_timeout):** Stop motors, show post-run stats, return to menu.

### Crossing Detection State Machine
- **CROSS_NONE:** Normal PID operation
- **CROSS_TRANSIT:** Driving straight through a wide-black zone for crossing_transit_ms
- Triggers when sumOnSensor ≥ crossing_thresh (default 10 of 14)
- Delegates turn execution to executeJunction() in turns.ino

---

## PHASE 5 — Bug Fixes During Review

### Bugs Found and Fixed After Initial Build

| Bug | File | Fix |
|---|---|---|
| `drawBatteryIcon()` defined in both pid.ino and display.ino | pid.ino | Removed from pid.ino |
| `drawSensorBars()` defined in both sensor.ino and display.ino | sensor.ino | Removed from sensor.ino |
| `drawRunningScreen()` defined in both pid.ino and display.ino | pid.ino | Removed from pid.ino |
| `cfg.batt_divider_ratio` used but field not in Settings struct | config.h | Added field to struct |
| C++ illegal aggregate struct initializer on existing object | system.ino | Changed to member-by-member |
| `handleCrossing()` had inline turns instead of calling turns.ino | pid.ino | Now calls executeJunction() |
| `drawSplashScreen()` / `drawFrontPage()` defined but never called | LFR_MASTER.ino | Added calls to setup() and loop() |
| `showLowBatteryWarning()` vs `drawLowBatteryWarning()` name mismatch | pid.ino | Unified to display.ino version |
| EEPROM overlap: Settings (~0xAD) overwrote Calibration (0x80) | config.h | Moved calibration to 0x100 |
| `readButtonBlocking()` in loop() blocked sensor display updates | LFR_MASTER.ino | Replaced with non-blocking pollButtons() |
| `crossState = CROSS_DETECTED` set then immediately overwritten | pid.ino | Removed dead intermediate state |
| `pollButtons()` called twice per menu iteration | menu.ino | Removed duplicate calls |

---

## PHASE 6 — Emulator Question

### What Works for Testing
**Wokwi** (wokwi.com) — best available option, supports STM32F103 (Blue Pill).
Can simulate OLED, buttons, basic GPIO.

### What Does NOT Work in Any Emulator
- Direct RCC register overclocking (configureCPU)
- Timer1 direct register PWM setup (initMotors)
- GPIO→ODR direct register writes (selectChannel)
- Accurate 12-bit ADC noise characteristics

### Simulation Workaround
```cpp
#define WOKWI_SIM  // comment out for real hardware
#ifdef WOKWI_SIM
  void configureCPU(uint8_t) {}
#endif
```

---

## FINAL FILE INVENTORY

### 02_LFR_MASTER (USE THIS — 3,552 lines total)

| File | Lines | Responsibility |
|---|---|---|
| config.h | 276 | All configuration — single source of truth |
| LFR_MASTER.ino | 210 | Globals, setup(), loop() |
| system.ino | 497 | CPU OC, battery, EEPROM, self-test, competition, stats |
| motor.ino | 295 | 20kHz PWM, dead-band, ramp, motor tests |
| sensor.ino | 297 | Atomic MUX, ADC oversampling, sensor test screens |
| calibration.ino | 354 | Auto/manual cal, 12-bit EEPROM, verify |
| pid.ino | 365 | Full PID, 5 modes, crossing FSM, lost-line FSM |
| turns.ino | 323 | Sensor-feedback turns, junction detection, distance |
| display.ino | 355 | All OLED drawing — single source |
| menu.ino | 580 | Complete menu, value editor, profiles |

### 01_8IR_UPGRADED (REFERENCE ONLY — 8-sensor Arduino, wrong hardware)
8 files, ~1,400 lines. Kept for reference. Do not flash.

---

*Summary generated: May 2026*
*Hardware: RoboTech Innovator BD 14-sensor STM32 LFR*
