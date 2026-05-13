#include "config.h"

void turnLeft(uint16_t ms, uint8_t speed) {
  setMotors(-speed, speed);
  delay(ms);
  motorBrake();
}

void turnRight(uint16_t ms, uint8_t speed) {
  setMotors(speed, -speed);
  delay(ms);
  motorBrake();
}

void turnUTurn(uint16_t ms, uint8_t speed) {
  turnLeft(ms*2, speed);
}

void driveStraight(uint16_t ms, int16_t speed) {
  setMotors(speed, speed);
  delay(ms);
}

void executeJunction(uint8_t type) {
  switch (type) {
    case JUNC_LEFT:  turnLeft(300, 180); break;
    case JUNC_RIGHT: turnRight(300, 180); break;
    default: driveStraight(settings.crossingTransitMs, settings.baseSpeed); break;
  }
}

void bridgeLostLine(uint16_t holdMs) {
  uint32_t start = millis();
  while (millis() - start < holdMs) {
    delay(5);
    if (millis() - start > holdMs + settings.spinTimeoutMs) break;
  }
}

void spinRecovery(uint16_t timeout) {
  uint32_t start = millis();
  while (millis() - start < timeout) {
    setMotors(100, -100);
    delay(20);
    int32_t dummy; uint8_t onC;
    readAllSensors(dummy, onC);
    if (onC > 0) break;
  }
  motorBrake();
}