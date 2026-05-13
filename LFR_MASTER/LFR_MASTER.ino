#include "config.h"

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
CalibrationData cal;
Settings settings;
PIDProfile* activePID = &settings.pidSafe;

uint8_t  menuState = 0;
bool     pidRunning = false;
bool     competitionActive = false;
uint8_t  crossingMode = CROSS_NONE;

// Menu system state
namespace Menu {
  uint8_t page = 0;
  uint8_t cursor = 0;
  uint8_t itemCount = 0;
  bool editing = false;
}

void setup() {
  AFIO->MAPR |= AFIO_MAPR_SWJ_CFG_JTAGDISABLE;  // free PB3,PB4,PA15
  configureCPU(settings.cpuSpeedMHz);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);

  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);
  drawSplashScreen();

  initADC();
  initMotors();

  loadSettings();
  if (!cal.calibrated) { /* mark invalid */ }
  switch (settings.activeProfile) {
    case 1: activePID = &settings.pidNormal; break;
    case 2: activePID = &settings.pidTurbo; break;
    default: activePID = &settings.pidSafe;
  }

  beep(50);
  drawFrontPage();
}

void loop() {
  pollButtons();

  if (menuState == 0) {
    if (isButtonPressed(BTN_SELECT)) {
      waitForButtonRelease();
      menuInit();
    }
  } else if (menuState == 1) {
    menuUpdate();
  } else if (menuState == 2) {
    if (!pidRunning) menuState = 1;
  }

  if (readBattery_mV() < settings.batteryCutoff && !pidRunning) {
    drawLowBatteryWarning();
    while (readBattery_mV() < settings.batteryCutoff) delay(500);
  }
}