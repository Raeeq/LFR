#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include <U8g2lib.h>

// ========== PINS ==========
#define MUX_S0          PB12
#define MUX_S1          PB13
#define MUX_S2          PB14
#define MUX_S3          PB15
#define MUX_ANALOG      PA0

#define MOTOR_LEFT_PWM   PA8
#define MOTOR_RIGHT_PWM  PA9
#define MOTOR_LEFT_DIR1  PB0
#define MOTOR_LEFT_DIR2  PB1
#define MOTOR_RIGHT_DIR1 PB10
#define MOTOR_RIGHT_DIR2 PB11

#define BTN_UP      PA4
#define BTN_DOWN    PA5
#define BTN_SELECT  PA6
#define BTN_BACK    PA7

#define BATTERY_PIN     PA1
#define BUZZER_PIN      PB3

// ========== CONSTANTS ==========
#define NUM_SENSORS      14
#define MUX_DELAY_US     10
#define ADC_OVERSAMPLE   3
#define MAX_PWM          255

// Lost‑line states
#define LOST_BRIDGE      0
#define LOST_SPIN        1
#define LOST_STOP        2

// Crossing states
#define CROSS_NONE       0
#define CROSS_DETECTED   1
#define CROSS_TRANSIT    2

// Junction types
#define JUNC_STRAIGHT    0
#define JUNC_LEFT        1
#define JUNC_RIGHT       2
#define JUNC_T_LEFT      3
#define JUNC_T_RIGHT     4
#define JUNC_CROSS       5
#define JUNC_END         6

// ========== EEPROM MAP ==========
#define EEPROM_CAL_START  0x100
#define EEPROM_SETTINGS   0x200
#define EEPROM_MARKER     0xA5

// ========== STRUCTURES (all integers) ==========
struct CalibrationData {
  uint16_t minVal[NUM_SENSORS];
  uint16_t maxVal[NUM_SENSORS];
  uint16_t refVal[NUM_SENSORS];
  bool calibrated;
};

// PID parameters stored as integer with fixed scaling
struct PIDProfile {
  int16_t kp;              // scaled by 1000
  int16_t ki;              // scaled by 1000
  int16_t kd;              // scaled by 1000
  int16_t derivativeAlpha; // scaled by 1000 (0..1000)
  int16_t integralLimit;   // raw (no scaling)
};

struct Settings {
  uint8_t  cpuSpeedMHz;      // 72, 96, 128
  uint8_t  lineMode;         // 0=dark on light, 1=light on dark
  uint8_t  adcResolution;    // 8,10,12 (used only for display)
  uint32_t serialBaud;
  uint8_t  telemetryMode;
  uint8_t  oledDuringRun;
  uint8_t  startDelay;       // sec
  uint16_t batteryCutoff;    // mV
  uint8_t  deadBandLeft;
  uint8_t  deadBandRight;
  uint16_t baseSpeed;        // 0..255
  uint16_t maxSpeed;
  uint16_t turboMultiplier;  // scaled by 100 (e.g., 130 = x1.3)
  uint16_t accelRampMs;
  uint8_t  lostLineMode;
  uint16_t bridgeHoldMs;
  uint16_t spinTimeoutMs;
  uint8_t  crossingMode;
  uint16_t crossingTransitMs;
  bool     lookAheadEnabled;
  uint8_t  edgeOffsetLeft;
  uint8_t  edgeOffsetRight;
  uint16_t battDividerRatio; // scaled by 100 (e.g., 200 = 2.0)
  PIDProfile pidSafe;
  PIDProfile pidNormal;
  PIDProfile pidTurbo;
  uint8_t  activeProfile;    // 0=Safe, 1=Normal, 2=Turbo
};

// ========== GLOBALS ==========
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern CalibrationData cal;
extern Settings settings;
extern PIDProfile* activePID;

extern uint8_t  menuState;
extern bool     pidRunning;
extern bool     competitionActive;
extern uint8_t  crossingMode;

// ========== FUNCTION PROTOTYPES ==========
// system.ino
void configureCPU(uint8_t speed);
void initADC();
void initMotors();
uint16_t readBattery_mV();
void saveSettings();
void loadSettings();
void resetFactory();
void beep(uint16_t ms);

// sensors.ino
void selectChannel(uint8_t ch);
uint16_t readSensor(uint8_t ch);
void readAllSensors(int32_t &position, uint8_t &onCount, uint16_t *rawOut = nullptr);
void autoCalibrate();
void manualCalibrateWhite();
void manualCalibrateBlack();
void verifyCalibration();
bool checkSensorHealth();

// motor.ino
void setMotors(int16_t leftPWM, int16_t rightPWM);
void motorBrake();
void motorCoast();

// pid.ino (all integer)
int16_t computePID(int32_t error);
void runPID(uint8_t mode);
void lostLineRecovery();
void handleCrossing();
void startCompetition(uint8_t profileIdx);
void showPostRunStats();

// turns.ino
void turnLeft(uint16_t ms, uint8_t speed);
void turnRight(uint16_t ms, uint8_t speed);
void turnUTurn(uint16_t ms, uint8_t speed);
void driveStraight(uint16_t ms, int16_t speed);
void executeJunction(uint8_t type);
void bridgeLostLine(uint16_t holdMs);
void spinRecovery(uint16_t timeoutMs);

// menu.ino
void menuInit();
void menuUpdate();
void menuDraw();
void pollButtons();
bool isButtonPressed(uint8_t btn);
void waitForButtonRelease();
void menuEditValue(const char* name, int32_t &value, int32_t step, int32_t minV, int32_t maxV, uint8_t decimals);

// display.ino
void drawSplashScreen();
void drawFrontPage();
void drawMenu(const char* title, const char** items, uint8_t count, uint8_t cursor, bool editing);
void drawRunningScreen(int32_t error, int16_t l, int16_t r, uint16_t spd, uint16_t bat);
void drawPostRunStats(uint32_t timeMs, uint16_t maxSpeed);
void drawBatteryIcon(uint16_t mV);
void drawSensorBars(uint16_t raw[NUM_SENSORS], uint8_t threshold);
void drawLowBatteryWarning();
void drawCalibrationProgress(uint8_t step);
void drawSimpleMessage(const char* line1, const char* line2 = nullptr);
void drawProgressBar(uint8_t pct);