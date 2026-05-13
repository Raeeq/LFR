#include "config.h"

void selectChannel(uint8_t ch) {
  GPIOB->ODR = (GPIOB->ODR & 0x0FFF) | ((ch & 0x0F) << 12);
  delayMicroseconds(MUX_DELAY_US);
}

uint16_t readSensor(uint8_t ch) {
  selectChannel(ch);
  uint32_t sum = 0;
  for (uint8_t i = 0; i < ADC_OVERSAMPLE; i++) {
    ADC1->CR2 |= ADC_CR2_ADON;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    while (!(ADC1->SR & ADC_SR_EOC));
    sum += ADC1->DR;
  }
  return sum / ADC_OVERSAMPLE;
}

void readAllSensors(int32_t &position, uint8_t &onCount, uint16_t *rawOut) {
  int32_t weighted = 0;
  uint32_t total = 0;
  onCount = 0;
  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    uint16_t raw = readSensor(i);
    if (rawOut) rawOut[i] = raw;
    uint32_t norm;
    if (cal.calibrated) {
      if (settings.lineMode == 0) { // dark line
        uint16_t clamped = constrain(raw, cal.minVal[i], cal.maxVal[i]);
        norm = map(clamped, cal.minVal[i], cal.maxVal[i], 0, 1000);
      } else { // light line
        uint16_t clamped = constrain(raw, cal.minVal[i], cal.maxVal[i]);
        norm = 1000 - map(clamped, cal.minVal[i], cal.maxVal[i], 0, 1000);
      }
    } else {
      norm = (raw > 500) ? 1000 : 0;
    }
    if (norm > 400) {
      onCount++;
      weighted += (int32_t)i * norm;
      total += norm;
    }
  }
  if (onCount > 0 && total > 0)
    position = weighted / (int32_t)total;
  else
    position = (NUM_SENSORS - 1) * 500; // center
}

void autoCalibrate() {
  drawSimpleMessage("Auto Calib", "Place on white");
  delay(2000);
  manualCalibrateWhite();
  delay(1000);
  drawSimpleMessage("Place on black");
  delay(2000);
  manualCalibrateBlack();
  cal.calibrated = true;
  EEPROM.put(EEPROM_CAL_START, cal);
  EEPROM.write(EEPROM_CAL_START, EEPROM_MARKER);
}

void manualCalibrateWhite() {
  for (uint8_t i = 0; i < NUM_SENSORS; i++) cal.minVal[i] = readSensor(i);
}

void manualCalibrateBlack() {
  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    cal.maxVal[i] = readSensor(i);
    cal.refVal[i] = (cal.minVal[i] + cal.maxVal[i]) / 2;
  }
}

void verifyCalibration() {
  bool pass = true;
  for (uint8_t i = 0; i < NUM_SENSORS; i++)
    if (cal.maxVal[i] - cal.minVal[i] < 100) pass = false;
  drawSimpleMessage(pass ? "CAL OK" : "CAL FAIL");
  delay(1500);
}

bool checkSensorHealth() {
  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    uint16_t v = readSensor(i);
    if (v < 50 || v > 4050) return false;
  }
  return true;
}