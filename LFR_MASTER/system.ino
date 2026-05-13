#include "config.h"

void configureCPU(uint8_t speed) {
  if (speed == 72) return;
  RCC->CR |= RCC_CR_HSEON;
  while (!(RCC->CR & RCC_CR_HSERDY));
  RCC->CFGR &= ~(0x3 << 16);   // clear PLLSRC
  RCC->CFGR |=  (0x2 << 16);   // HSE as PLL source, no PREDIV
  if (speed == 96) {
    RCC->CFGR |= RCC_CFGR_PLLMULL12;  // 8*12=96
  } else if (speed == 128) {
    RCC->CFGR |= RCC_CFGR_PLLMULL16;  // 8*16=128
  }
  FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
  RCC->CR |= RCC_CR_PLLON;
  while (!(RCC->CR & RCC_CR_PLLRDY));
  RCC->CFGR &= ~RCC_CFGR_SW;
  RCC->CFGR |= RCC_CFGR_SW_PLL;
  while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
  SystemCoreClock = speed * 1000000;
  SysTick_Config(SystemCoreClock / 1000);
}

void initADC() {
  pinMode(MUX_ANALOG, INPUT_ANALOG);
  RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
  ADC1->SQR1 = 0;
  ADC1->SQR3 = 0;          // ch0 (PA0)
  ADC1->SMPR2 = ADC_SMPR2_SMP0_2; // 55.5 cycles
}

void initMotors() {
  pinMode(MOTOR_LEFT_DIR1, OUTPUT);
  pinMode(MOTOR_LEFT_DIR2, OUTPUT);
  pinMode(MOTOR_RIGHT_DIR1, OUTPUT);
  pinMode(MOTOR_RIGHT_DIR2, OUTPUT);

  RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
  // PA8, PA9 alternate function push‑pull, 50 MHz
  GPIOA->CRH &= ~(GPIO_CRH_MODE8 | GPIO_CRH_CNF8 | GPIO_CRH_MODE9 | GPIO_CRH_CNF9);
  GPIOA->CRH |=  (GPIO_CRH_MODE8_0 | GPIO_CRH_MODE8_1 | GPIO_CRH_CNF8_1 |
                  GPIO_CRH_MODE9_0 | GPIO_CRH_MODE9_1 | GPIO_CRH_CNF9_1);
  TIM1->PSC = 36 - 1;      // 72MHz/36 = 2MHz
  TIM1->ARR = 100 - 1;     // 20kHz
  TIM1->CCMR1 = TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2 |
                TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2M_2;
  TIM1->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E;
  TIM1->BDTR = TIM_BDTR_MOE;
  TIM1->CCR1 = 0; TIM1->CCR2 = 0;
  TIM1->CR1 = TIM_CR1_CEN;
}

uint16_t readBattery_mV() {
  uint32_t raw = analogRead(BATTERY_PIN);
  uint32_t mv = raw * 3300UL / 4095;                  // 3.3V reference
  mv = mv * settings.battDividerRatio / 100;          // divider ratio scaled *100
  return (uint16_t)mv;
}

void saveSettings() {
  EEPROM.put(EEPROM_SETTINGS, settings);
  EEPROM.write(EEPROM_SETTINGS, EEPROM_MARKER);
}

void loadSettings() {
  if (EEPROM.read(EEPROM_SETTINGS) == EEPROM_MARKER) {
    EEPROM.get(EEPROM_SETTINGS, settings);
  } else {
    resetFactory();
    saveSettings();
  }
}

void resetFactory() {
  settings.cpuSpeedMHz = 72;
  settings.lineMode = 0;
  settings.adcResolution = 12;
  settings.serialBaud = 115200;
  settings.telemetryMode = 0;
  settings.oledDuringRun = 0;
  settings.startDelay = 3;
  settings.batteryCutoff = 6000;
  settings.deadBandLeft = 30;
  settings.deadBandRight = 30;
  settings.baseSpeed = 150;
  settings.maxSpeed = 255;
  settings.turboMultiplier = 130;   // 1.30
  settings.accelRampMs = 200;
  settings.lostLineMode = 0;
  settings.bridgeHoldMs = 200;
  settings.spinTimeoutMs = 1500;
  settings.crossingMode = 0;
  settings.crossingTransitMs = 300;
  settings.lookAheadEnabled = true;
  settings.edgeOffsetLeft = 2;
  settings.edgeOffsetRight = 11;
  settings.battDividerRatio = 200;  // 2.0
  settings.pidSafe   = {500, 10, 1200, 400, 50};
  settings.pidNormal = {800, 20, 1800, 350, 60};
  settings.pidTurbo  = {1200, 30, 2500, 300, 70};
  settings.activeProfile = 0;
}

void beep(uint16_t ms) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ms);
  digitalWrite(BUZZER_PIN, LOW);
}