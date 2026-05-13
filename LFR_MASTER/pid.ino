#include "config.h"

static int32_t prevError = 0;
static int32_t integral = 0;
static int32_t filteredDerivative = 0;
static uint32_t lastPIDTime = 0;
static uint16_t currentSpeed = 0;

// Error is sensor position - setpoint (range approx ±13000)
int16_t computePID(int32_t error) {
  uint32_t now = millis();
  int32_t dt_ms = (int32_t)(now - lastPIDTime);
  if (dt_ms <= 0) dt_ms = 5;
  lastPIDTime = now;

  // Proportional (kp scaled by 1000)
  int32_t pTerm = ((int32_t)activePID->kp * error) / 1000;

  // Integral with anti-windup (ki scaled by 1000)
  integral += error * dt_ms;
  int32_t maxI = (int32_t)activePID->integralLimit * 1000 / (activePID->ki ? activePID->ki : 1);
  if (integral > maxI) integral = maxI;
  if (integral < -maxI) integral = -maxI;
  int32_t iTerm = (activePID->ki * integral) / 1000;

  // Filtered derivative
  int32_t rawDeriv = ((error - prevError) * 1000) / dt_ms;  // units: error/sec
  filteredDerivative = (activePID->derivativeAlpha * rawDeriv +
                        (1000 - activePID->derivativeAlpha) * filteredDerivative) / 1000;
  int32_t dTerm = (activePID->kd * filteredDerivative) / 1000;

  prevError = error;
  int32_t out = pTerm + iTerm + dTerm;
  if (out > 255) out = 255;
  if (out < -255) out = -255;
  return (int16_t)out;
}

void runPID(uint8_t mode) {
  pidRunning = true;
  currentSpeed = settings.baseSpeed / 2;
  uint16_t targetSpeed = settings.baseSpeed;
  if (mode == 4) targetSpeed = (uint16_t)((uint32_t)settings.baseSpeed * settings.turboMultiplier / 100);

  integral = 0;
  prevError = 0;
  filteredDerivative = 0;
  lastPIDTime = millis();

  while (pidRunning) {
    pollButtons();
    if (isButtonPressed(BTN_SELECT)) { waitForButtonRelease(); pidRunning = false; break; }
    if (isButtonPressed(BTN_BACK)) {
      waitForButtonRelease();
      targetSpeed = (targetSpeed == settings.baseSpeed) ?
                    (uint16_t)((uint32_t)settings.baseSpeed * settings.turboMultiplier / 100) :
                    settings.baseSpeed;
    }

    int32_t position; uint8_t onCount;
    readAllSensors(position, onCount);
    int32_t setpoint = (NUM_SENSORS-1)*500;
    if (mode == 2) setpoint = settings.edgeOffsetLeft * 1000;
    else if (mode == 3) setpoint = settings.edgeOffsetRight * 1000;
    int32_t error = position - setpoint;
    if (onCount == 0) error = (prevError > 0) ? 13000 : -13000;

    // Crossing
    if (onCount >= 10 && crossingMode == CROSS_NONE) {
      crossingMode = CROSS_DETECTED;
      executeJunction(JUNC_STRAIGHT);
    } else if (onCount < 10) {
      crossingMode = CROSS_NONE;
    }

    // Lost line
    if (onCount == 0) {
      lostLineRecovery();
      if (!pidRunning) break;
    }

    int16_t pidOut = computePID(error);
    // Speed ramp
    if (currentSpeed < targetSpeed) {
      currentSpeed += (targetSpeed - settings.baseSpeed/2) / (settings.accelRampMs / 10 + 1);
      if (currentSpeed > targetSpeed) currentSpeed = targetSpeed;
    }
    int16_t leftPWM  = constrain((int16_t)targetSpeed + pidOut, 0, (int)currentSpeed);
    int16_t rightPWM = constrain((int16_t)targetSpeed - pidOut, 0, (int)currentSpeed);
    setMotors(leftPWM, rightPWM);

    if (settings.oledDuringRun == 0) {
      drawRunningScreen(error, leftPWM, rightPWM, currentSpeed, readBattery_mV());
    }
    delay(5);
  }
  motorBrake();
  showPostRunStats();
  pidRunning = false;
  menuState = 1;
}

void lostLineRecovery() {
  switch (settings.lostLineMode) {
    case 0: bridgeLostLine(settings.bridgeHoldMs); break;
    case 1: spinRecovery(settings.spinTimeoutMs); break;
    default: pidRunning = false; break;
  }
}

void startCompetition(uint8_t idx) {
  switch (idx) {
    case 0: activePID = &settings.pidSafe; break;
    case 1: activePID = &settings.pidNormal; break;
    case 2: activePID = &settings.pidTurbo; break;
  }
  // Countdown omitted for brevity
  runPID(0);
}

void showPostRunStats() {
  drawSimpleMessage("Run Complete", "Press SELECT");
  while (!isButtonPressed(BTN_SELECT)) { pollButtons(); delay(10); }
  waitForButtonRelease();
}

void handleCrossing() {
  executeJunction(JUNC_STRAIGHT);
}