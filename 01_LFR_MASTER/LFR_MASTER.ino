// ╔══════════════════════════════════════════════════════════════════╗
// ║              LFR MASTER  v3.0.0                                 ║
// ║  STM32F103C8T6 (Blue Pill)  ·  14-sensor curved wing array     ║
// ║  12-bit ADC  ·  Overclocked  ·  Full PID  ·  Menu system       ║
// ╚══════════════════════════════════════════════════════════════════╝

#include "config.h"

// ─── OLED ────────────────────────────────────────────────────────
U8GLIB_SSD1306_128X64 u8g(U8G_I2C_OPT_DEV_0 | U8G_I2C_OPT_NO_ACK | U8G_I2C_OPT_FAST);

// ─── SENSOR GLOBALS ──────────────────────────────────────────────
int   sensorADC[SENSOR_COUNT];         // Raw 12-bit ADC (0–4095)
int   sensorDigital[SENSOR_COUNT];     // Thresholded (0 or 1)
int   Max_ADC[SENSOR_COUNT];           // Calibration max per sensor
int   Min_ADC[SENSOR_COUNT];           // Calibration min per sensor
int   Reference_ADC[SENSOR_COUNT];     // Calibration threshold (midpoint)
int   sumOnSensor;                     // Active sensor count
int   weightedSum;                     // Sum of WeightValue[i] for active sensors
int   bitSensor;                       // Binary pattern (C13=MSB, C0=LSB)

// Position weight per sensor: C0=10 (leftmost), C13=140 (rightmost)
// Center of range = 75.0 — see CENTER_POSITION in config.h
const int WeightValue[SENSOR_COUNT] = {
  10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140
};

// Binary encoding: sensor 0 = LSB (bit 0), sensor 13 = bit 13
const uint16_t bitWeight[SENSOR_COUNT] = {
  1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192
};

// Look-ahead multipliers for angled outer wing sensors (C0–C2, C11–C13).
// These sensors are physically angled outward on the PCB and see curves
// before the center sensors do. Higher multiplier = stronger early response.
// ← Tune LOOKAHEAD_C* in config.h to adjust aggressiveness.
const float lookaheadMult[SENSOR_COUNT] = {
  LOOKAHEAD_C0_C13, LOOKAHEAD_C1_C12, LOOKAHEAD_C2_C11,  // C0–C2: left wing
  1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,        // C3–C10: center
  LOOKAHEAD_C2_C11, LOOKAHEAD_C1_C12, LOOKAHEAD_C0_C13   // C11–C13: right wing
};

// ─── PID GLOBALS ─────────────────────────────────────────────────
float line_position   = CENTER_POSITION;
float error           = 0.0f;
float previous_error  = 0.0f;
float filtered_deriv  = 0.0f;
float integral        = 0.0f;
float pid_target      = CENTER_POSITION;  // changes per FollowMode

// ─── SETTINGS & PROFILES ────────────────────────────────────────
Settings cfg;    // All runtime settings, loaded from EEPROM on boot

// Convenience pointer to active profile (cfg.profiles[cfg.active_profile])
// Use activeP.kp, activeP.kd etc. throughout pid.ino
PIDProfile* activeP = nullptr;

// ─── LIVE TUNING (adjusted via buttons during PID run) ──────────
int   live_speed_offset = 0;   // Added to activeP.base_speed during run (+/- 50 max)
bool  turbo_active      = false;

// ─── RUN STATISTICS ─────────────────────────────────────────────
RunStats  lastStats;
RunStats  currentStats;
uint32_t  runStartTime;

// ─── BATTERY ────────────────────────────────────────────────────
float    batteryMV       = 8400.0f;  // mV, updated every BATT_SAMPLE_INTERVAL_MS
bool     batteryLow      = false;
bool     batteryCritical = false;
uint32_t lastBatteryRead = 0;

// ─── CROSSING DETECTION STATE MACHINE ───────────────────────────
typedef enum : uint8_t {
  CROSS_NONE     = 0,  // normal PID operation
  CROSS_DETECTED = 1,  // wide-black zone just entered
  CROSS_TRANSIT  = 2   // driving straight through crossing
} CrossState;
CrossState crossState    = CROSS_NONE;
uint32_t   crossStart    = 0;
uint8_t    crossSeqIdx   = 0;   // index into user-programmed crossing sequence

// Optional crossing sequence — 0=straight, 1=left, 2=right
// Edit this array to program a specific route at junctions.
// ← CHANGE this sequence to match your track's intersection decisions.
uint8_t crossingSequence[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
#define CROSSING_SEQ_LEN  (sizeof(crossingSequence) / sizeof(crossingSequence[0]))

// ─── LOST-LINE STATE ─────────────────────────────────────────────
uint32_t lostLineStart  = 0;
bool     isLost         = false;
float    lastGoodError  = 0.0f;   // direction to spin when lost

// ─── MOTOR STATE (for ramping) ───────────────────────────────────
float rampedLeft  = 0.0f;   // current ramped PWM, left motor
float rampedRight = 0.0f;   // current ramped PWM, right motor

// ─── MENU STATE ─────────────────────────────────────────────────
// remembered cursor positions per submenu, indexed by menu level
int  menuSel[12] = {0};

// ─── OLED BITMAPS (unchanged from original design) ───────────────
const unsigned char PROGMEM bitmap_item_sel_outline[] = {
  0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30,
  0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe0,
  0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0
};

const unsigned char PROGMEM bitmap_scrollbar_bg[] = {
  0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02,
  0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02,
  0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02,
  0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02,
  0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02,
  0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02,
  0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02,
  0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00
};

// ═════════════════════════════════════════════════════════════════
// SETUP
// ═════════════════════════════════════════════════════════════════
void setup() {
  // ── Step 1: Configure CPU clock FIRST, before any timing-dependent init
  // This ensures millis() and PWM frequencies are correct from the start.
  loadSettings();           // reads cfg from EEPROM (or defaults if invalid)
  configureCPU(cfg.cpu_speed_idx);  // sets PLL, flash wait states, SysTick

  // ── Step 2: Serial (baud set from saved config)
  Serial.begin(cfg.serial_baud);
  Serial.println(F("LFR MASTER v" FIRMWARE_VERSION " booting..."));

  // ── Step 3: GPIO
  // MUX — output
  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  pinMode(MUX_S3, OUTPUT);
  // Sensor ADC input
  pinMode(MUX_SIG, INPUT_ANALOG);
  analogReadResolution(cfg.adc_bits);   // 12-bit by default

  // Buttons — active LOW with internal pullups
  pinMode(BTN_UP,     INPUT_PULLUP);
  pinMode(BTN_DOWN,   INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK,   INPUT_PULLUP);

  // LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF);

  // Motors
  initMotors();   // defined in motor.ino — sets up pins + 20 kHz PWM timer

  // ── Step 4: OLED
  u8g.setColorIndex(1);
  drawSplashScreen();   // brief intro screen while rest of setup runs

  // ── Step 5: Active profile pointer
  activeP = &cfg.profiles[cfg.active_profile];

  // ── Step 6: Load calibration from EEPROM
  loadCalibration();

  // ── Step 7: Boot self-test (shows results on OLED)
  runSelfTest();

  // ── Step 8: First battery read
  updateBattery(true);

  Serial.print(F("CPU: "));
  Serial.print(SystemCoreClock / 1000000);
  Serial.println(F(" MHz"));
  Serial.print(F("Battery: "));
  Serial.print(batteryMV / 1000.0f, 2);
  Serial.println(F(" V"));
  Serial.println(F("Ready."));
}

// ═════════════════════════════════════════════════════════════════
// LOOP — live sensor display on front page
// Press DOWN to open menu
// ═════════════════════════════════════════════════════════════════
void loop() {
  while (1) {
    readSensors();
    updateBattery(false);
    drawFrontPage();   // defined in display.ino — single source for home screen

    // Non-blocking check — display keeps refreshing every iteration
    // readButtonBlocking() would freeze the sensor display until pressed
    if (pollButtons() == BTN_DOWN_SHORT) {
      menuRoot();
    }
  }
}
