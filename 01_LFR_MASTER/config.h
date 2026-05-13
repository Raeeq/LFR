#pragma once
// ╔══════════════════════════════════════════════════════════════════╗
// ║              LFR MASTER  —  config.h                            ║
// ║  Single source of truth for every configurable constant.        ║
// ║  Change values here. Never hunt through multiple files.         ║
// ╚══════════════════════════════════════════════════════════════════╝

#define FIRMWARE_VERSION  "3.0.0"
#define FIRMWARE_DATE     "2026-05"

// ── INCLUDE ───────────────────────────────────────────────────────
#include "U8glib.h"
#include <EEPROM.h>

// ═════════════════════════════════════════════════════════════════
// CPU OVERCLOCKING
// ═════════════════════════════════════════════════════════════════
// ⚠ OC WARNING: STM32F103 is spec'd at 72 MHz max.
//   96/128 MHz are well-documented community overclocks. Stable on
//   99% of Blue Pill chips. Generates moderate heat during short runs.
//   Always test at 72 first, then step up.
//   0 = 72 MHz  (safe,  stock)
//   1 = 96 MHz  (fast,  recommended minimum for competition)
//   2 = 128 MHz (maximum — PLL × 16 from 8 MHz HSE)
#define DEFAULT_CPU_SPEED_IDX   2   // ← change if unstable

// ═════════════════════════════════════════════════════════════════
// SENSOR PINS  (confirmed from hardware diagram)
// ═════════════════════════════════════════════════════════════════
#define MUX_S0          PB15
#define MUX_S1          PB14
#define MUX_S2          PB13
#define MUX_S3          PB12
#define MUX_SIG         PA0     // Analog in — 12-bit ADC on STM32
#define SENSOR_COUNT    14

// ═════════════════════════════════════════════════════════════════
// MOTOR PINS  ← ⚠ CHANGE THESE TO MATCH YOUR WIRING
// ═════════════════════════════════════════════════════════════════
// PA8/PA9 = Timer1 CH1/CH2 — hardware PWM up to 20 kHz
#define MOTOR_L_PWM     PA8    // ← Left  motor speed (PWM)
#define MOTOR_L_DIR     PB0    // ← Left  motor direction
#define MOTOR_R_PWM     PA9    // ← Right motor speed (PWM)
#define MOTOR_R_DIR     PB1    // ← Right motor direction

// Driver wiring convention:
// 0 = PWM + DIR  (one speed pin + one direction pin per motor)
// 1 = IN1 + IN2  (L298N style — see motor.ino for details)
// ← CHANGE THIS if your driver differs from PWM+DIR
#define MOTOR_DRIVER_TYPE   0

// ═════════════════════════════════════════════════════════════════
// BUTTON PINS  (active LOW, INPUT_PULLUP)
// ═════════════════════════════════════════════════════════════════
// ← CHANGE if your physical wiring differs
#define BTN_UP          PA4
#define BTN_DOWN        PA5
#define BTN_SELECT      PA6
#define BTN_BACK        PA7

// ═════════════════════════════════════════════════════════════════
// BATTERY MONITORING
// ═════════════════════════════════════════════════════════════════
// Wire: LiPo+ → R1 → PA1 → R2 → GND
// Default resistors: R1 = 100 kΩ, R2 = 47 kΩ
//   Ratio = (100+47)/47 = 3.128  →  8.4V full → 2.69V at PA1 ✓
// ← CHANGE RATIO if you use different resistors: (R1+R2)/R2
#define BATTERY_PIN             PA1
#define BATT_DIVIDER_RATIO      3.128f
#define BATT_WARN_MV            7000    // mV — OLED warning threshold
#define BATT_CUTOFF_MV          6600    // mV — auto-stop to protect LiPo
#define BATT_SAMPLE_INTERVAL_MS 500     // how often to re-read battery

// ═════════════════════════════════════════════════════════════════
// LED
// ═════════════════════════════════════════════════════════════════
#define LED_PIN     PC13    // Blue Pill onboard LED
#define LED_ON      LOW     // ← Blue Pill LED is ACTIVE LOW
#define LED_OFF     HIGH

// ═════════════════════════════════════════════════════════════════
// SENSOR CONFIG
// ═════════════════════════════════════════════════════════════════
#define ADC_SAMPLES       3       // Oversampling (1–4). 3 = good noise vs speed.
#define ADC_MAX           4095    // 12-bit range. Updates if resolution changes.
#define MUX_SETTLE_US     5       // µs after channel select before ADC read.
                                  // CD74HC4067 settles in 200 ns. 5µs = 25× margin.

// Position weight for each sensor (C0=left=10, C13=right=140)
// Center = midpoint = 75.0
// ← Keep evenly spaced. Only change step if you redesign weights.
#define WEIGHT_STEP       10
#define CENTER_POSITION   75.0f

// Look-ahead multipliers for the angled outer "wing" sensors.
// C0–C2 and C11–C13 are physically angled outward on the PCB.
// When they activate, a sharp curve is approaching.
// Higher value = earlier, stronger curve response.
// ← Tune multipliers if robot over/under-reacts to curves.
#define LOOKAHEAD_C0_C13  2.2f   // extreme outer
#define LOOKAHEAD_C1_C12  1.8f   // mid outer
#define LOOKAHEAD_C2_C11  1.4f   // inner outer
// Center sensors C3–C10 use multiplier 1.0 (no boost)

// ═════════════════════════════════════════════════════════════════
// DEFAULT PID PROFILES
// ═════════════════════════════════════════════════════════════════
// Profile A — SAFE (calibration, learning the track)
#define PROF_A_KP          8.0f
#define PROF_A_KI          0.0f
#define PROF_A_KD          500.0f
#define PROF_A_BASE_SPD    150
#define PROF_A_MAX_SPD     210
#define PROF_A_TURBO       1.35f
#define PROF_A_ALPHA       0.40f   // derivative LPF (0=max smooth, 1=raw)
#define PROF_A_ILIMIT      60.0f
#define PROF_A_RAMP_MS     100     // acceleration ramp time

// Profile B — NORMAL (main competition profile)
#define PROF_B_KP          13.0f
#define PROF_B_KI          0.0f
#define PROF_B_KD          750.0f
#define PROF_B_BASE_SPD    200
#define PROF_B_MAX_SPD     255
#define PROF_B_TURBO       1.40f
#define PROF_B_ALPHA       0.35f
#define PROF_B_ILIMIT      60.0f
#define PROF_B_RAMP_MS     70

// Profile C — TURBO (when you know the track cold)
#define PROF_C_KP          18.0f
#define PROF_C_KI          0.0f
#define PROF_C_KD          1000.0f
#define PROF_C_BASE_SPD    240
#define PROF_C_MAX_SPD     255
#define PROF_C_TURBO       1.50f
#define PROF_C_ALPHA       0.30f
#define PROF_C_ILIMIT      50.0f
#define PROF_C_RAMP_MS     50

// ═════════════════════════════════════════════════════════════════
// LOST-LINE & CROSSING DEFAULTS
// ═════════════════════════════════════════════════════════════════
#define DEFAULT_BRIDGE_HOLD_MS    80     // hold last PWM output (bridges dashes)
#define DEFAULT_SPIN_TIMEOUT_MS   1500   // max spin-search before stopping
#define DEFAULT_CROSSING_THRESH   10     // sensors active = wide-black/intersection
#define DEFAULT_CROSS_TRANSIT_MS  220    // drive straight through a crossing

// ═════════════════════════════════════════════════════════════════
// BUTTON TIMING
// ═════════════════════════════════════════════════════════════════
#define BTN_DEBOUNCE_MS      25    // ignore bounces shorter than this
#define BTN_HOLD_INITIAL_MS  600   // hold time before fast-scroll activates
#define BTN_HOLD_FAST_MS     80    // interval between increments during fast scroll

// ═════════════════════════════════════════════════════════════════
// EEPROM LAYOUT  — 2 bytes per uint16/int16, 4 bytes per float
// ⚠ Do NOT change addresses once calibration is saved.
//   If you change layout, increment EEPROM_VERSION to invalidate old data.
//   Settings struct (with 3 PIDProfiles) is ~171 bytes with alignment padding.
//   Calibration starts at 0x100 (256) — leaves 253 bytes of safe margin.
// ═════════════════════════════════════════════════════════════════
#define EEPROM_VERSION          0xBE04    // bumped: layout changed, old data invalidated
#define EEPROM_MAGIC_ADDR       0x000     // 2 bytes
#define EEPROM_SETTINGS_ADDR    0x002     // Settings struct (~171 bytes with padding)
// 0x002 + 171 = 0x0AD — calibration starts well above this at 0x100
#define EEPROM_CALIB_REF_ADDR   0x100     // Reference_ADC[14]: 14 × 2 = 28 bytes
#define EEPROM_CALIB_MAX_ADDR   0x11C     // Max_ADC[14]: 28 bytes
#define EEPROM_CALIB_MIN_ADDR   0x138     // Min_ADC[14]: 28 bytes
// End: 0x154 = 340 bytes used out of 1024 available. No overlap possible.

// ═════════════════════════════════════════════════════════════════
// ENUMS
// ═════════════════════════════════════════════════════════════════
typedef enum : uint8_t {
  FOLLOW_STANDARD = 0,   // PID toward center
  FOLLOW_CENTER   = 1,   // aggressive center-snap (non-linear correction)
  FOLLOW_LEFT     = 2,   // follow left edge of line
  FOLLOW_RIGHT    = 3,   // follow right edge of line
  FOLLOW_TURBO    = 4    // full speed, active profile × turbo_mult
} FollowMode;

typedef enum : uint8_t {
  LOST_BRIDGE = 0,   // hold last PWM — best for dashed lines ✓
  LOST_SPIN   = 1,   // rotate toward last known direction
  LOST_STOP   = 2    // stop immediately
} LostLineMode;

typedef enum : uint8_t {
  CROSS_STRAIGHT  = 0,   // go straight through every intersection
  CROSS_SEQUENCE  = 1,   // follow programmed sequence array
  CROSS_LEFT      = 2,   // always turn left at every intersection
  CROSS_RIGHT     = 3    // always turn right at every intersection
} CrossingAction;

typedef enum : uint8_t {
  CPU_72MHz  = 0,
  CPU_96MHz  = 1,
  CPU_128MHz = 2
} CPUSpeedIdx;

typedef enum : uint8_t {
  TELEM_OFF    = 0,
  TELEM_ERROR  = 1,   // error + correction only (low bandwidth)
  TELEM_FULL   = 2    // all PID vars (for Serial Plotter)
} TelemetryMode;

typedef enum : uint8_t {
  BTN_NONE          = 0,
  BTN_UP_SHORT      = 1,
  BTN_UP_HOLD       = 2,
  BTN_DOWN_SHORT    = 3,
  BTN_DOWN_HOLD     = 4,
  BTN_SELECT_SHORT  = 5,
  BTN_SELECT_LONG   = 6,
  BTN_BACK_SHORT    = 7,
  BTN_BACK_LONG     = 8
} ButtonEvent;

// ═════════════════════════════════════════════════════════════════
// STRUCTS
// ═════════════════════════════════════════════════════════════════
typedef struct {
  float   kp, ki, kd;
  int16_t base_speed;
  int16_t max_speed;
  float   turbo_mult;
  float   deriv_alpha;    // 0.0 = max smooth, 1.0 = raw derivative
  float   integral_max;
  int16_t accel_ramp_ms;
  char    name[6];        // "SAFE ", "NORM ", "TRBO "
} PIDProfile;

typedef struct {
  uint16_t magic;
  // System
  uint8_t  cpu_speed_idx;
  uint8_t  line_mode;         // 0=black-on-white, 1=white-on-black
  uint8_t  adc_bits;          // 8, 10, or 12
  // Lost line
  uint8_t  lost_mode;
  uint16_t bridge_hold_ms;
  uint16_t spin_timeout_ms;
  // Crossing
  uint8_t  crossing_thresh;
  uint16_t crossing_transit_ms;
  uint8_t  crossing_action;
  // Driving
  int16_t  left_edge_offset;  // in weight units × 10
  int16_t  right_edge_offset;
  uint8_t  lookahead_en;
  // Hardware
  uint8_t  dead_band_left;
  uint8_t  dead_band_right;
  uint16_t batt_cutoff_mv;
  float    batt_divider_ratio;  // (R1+R2)/R2 — matches BATT_DIVIDER_RATIO default
  // Display / IO
  uint8_t  oled_dim_run;      // 0=on, 1=dim, 2=off during PID run
  uint8_t  telemetry_mode;
  uint32_t serial_baud;
  // Competition
  uint8_t  start_delay_sec;
  uint8_t  active_profile;    // 0=A, 1=B, 2=C
  // Profiles
  PIDProfile profiles[3];
} Settings;

typedef struct {
  uint32_t run_time_ms;
  uint16_t lost_line_events;
  uint16_t crossing_events;
  int16_t  max_speed_reached;
  float    avg_error;
  float    max_error;
  float    min_battery_mv;
} RunStats;
