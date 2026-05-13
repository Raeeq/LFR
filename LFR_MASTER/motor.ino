#include "config.h"

void setMotors(int16_t left, int16_t right) {
  left = constrain(left, -MAX_PWM, MAX_PWM);
  right = constrain(right, -MAX_PWM, MAX_PWM);
  if (abs(left) < settings.deadBandLeft) left = 0;
  if (abs(right) < settings.deadBandRight) right = 0;

  digitalWrite(MOTOR_LEFT_DIR1, left >= 0 ? HIGH : LOW);
  digitalWrite(MOTOR_LEFT_DIR2, left >= 0 ? LOW : HIGH);
  digitalWrite(MOTOR_RIGHT_DIR1, right >= 0 ? HIGH : LOW);
  digitalWrite(MOTOR_RIGHT_DIR2, right >= 0 ? LOW : HIGH);

  TIM1->CCR1 = abs(left);
  TIM1->CCR2 = abs(right);
}

void motorBrake() {
  digitalWrite(MOTOR_LEFT_DIR1, HIGH);
  digitalWrite(MOTOR_LEFT_DIR2, HIGH);
  digitalWrite(MOTOR_RIGHT_DIR1, HIGH);
  digitalWrite(MOTOR_RIGHT_DIR2, HIGH);
  TIM1->CCR1 = TIM1->CCR2 = 0;
}

void motorCoast() {
  digitalWrite(MOTOR_LEFT_DIR1, LOW);
  digitalWrite(MOTOR_LEFT_DIR2, LOW);
  digitalWrite(MOTOR_RIGHT_DIR1, LOW);
  digitalWrite(MOTOR_RIGHT_DIR2, LOW);
  TIM1->CCR1 = TIM1->CCR2 = 0;
}