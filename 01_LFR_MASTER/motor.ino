// ╔══════════════════════════════════════════════════════════════════╗
// ║  motor.ino  —  Motor Control                                    ║
// ║  20 kHz hardware PWM · Dead-band compensation · Accel ramp     ║
// ║  Supports PWM+DIR and IN1+IN2 driver types                     ║
// ╚══════════════════════════════════════════════════════════════════╝

// ─── PWM FREQUENCY ───────────────────────────────────────────────
// 20 kHz is above human hearing — no motor whine.
// Better efficiency than default 1 kHz from analogWrite().
// Uses Timer1 (PA8 = CH1, PA9 = CH2) which is on APB2 (full speed).
#define MOTOR_PWM_FREQ_HZ   20000UL

// ─── TIMER1 PERIOD ───────────────────────────────────────────────
// Calculated at runtime after CPU clock is set:
//   period = SystemCoreClock / MOTOR_PWM_FREQ_HZ - 1
// Stored here so applyMotors() can reference it without recalculating.
static uint32_t tim1Period = 0;

// ═════════════════════════════════════════════════════════════════
// INIT MOTORS — call once from setup() AFTER configureCPU()
// Configures Timer1 for 20 kHz complementary PWM on PA8/PA9.
// ═════════════════════════════════════════════════════════════════
void initMotors() {
  // ── Direction pins
  pinMode(MOTOR_L_DIR, OUTPUT);
  pinMode(MOTOR_R_DIR, OUTPUT);
  digitalWrite(MOTOR_L_DIR, LOW);
  digitalWrite(MOTOR_R_DIR, LOW);

  // ── Enable Timer1 and GPIOA clocks
  RCC->APB2ENR |= RCC_APB2ENR_TIM1EN | RCC_APB2ENR_IOPAEN;

  // ── Configure PA8 and PA9 as Alternate Function Push-Pull (50 MHz)
  // PA8 = bits [3:2] in CRH, PA9 = bits [7:4]
  GPIOA->CRH = (GPIOA->CRH
    & ~(0xFF << 0))           // clear bits for PA8, PA9
    | (0xBB << 0);            // 0xB = Alt func push-pull, 50 MHz for both

  // ── Timer1 setup
  tim1Period = (SystemCoreClock / MOTOR_PWM_FREQ_HZ) - 1;

  TIM1->CR1   = 0;                      // disable while configuring
  TIM1->PSC   = 0;                      // no prescaler — full CPU clock
  TIM1->ARR   = tim1Period;             // auto-reload = period

  // CH1 (PA8) and CH2 (PA9) — PWM mode 1 (active while CNT < CCR)
  TIM1->CCMR1 = (TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1PE)  // CH1
              | (TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2PE); // CH2

  TIM1->CCER  = TIM_CCER_CC1E | TIM_CCER_CC2E;  // enable CH1 and CH2 output
  TIM1->BDTR  = TIM_BDTR_MOE;                    // main output enable (REQUIRED for TIM1)
  TIM1->CCR1  = 0;                               // duty 0 — motors off
  TIM1->CCR2  = 0;
  TIM1->CR1   = TIM_CR1_ARPE | TIM_CR1_CEN;     // enable with auto-reload preload

  // Ensure motors are off
  motor(0, 0);
}

// ═════════════════════════════════════════════════════════════════
// CORE MOTOR FUNCTION
// LPWM and RPWM range: -255 (full reverse) to +255 (full forward)
// Dead-band compensation ensures linear response from the very first
// motion command — without it, low PWM values spin nothing.
// ═════════════════════════════════════════════════════════════════
void motor(int LPWM, int RPWM) {
  // ── Clamp FIRST so all downstream logic uses the final values
  LPWM = constrain(LPWM, -255, 255);
  RPWM = constrain(RPWM, -255, 255);

  // ── Dead-band compensation
  // Scales non-zero values so that input 1 maps to dead_band+1,
  // and input 255 still maps to 255. Zero stays zero (full coast).
  // ← Calibrate dead_band_left/right via Motor Test → Dead Band Test
  if (LPWM != 0) {
    int db = cfg.dead_band_left;
    LPWM = (LPWM > 0)
      ? db + (int)((255 - db) * LPWM / 255.0f)
      : -(db + (int)((255 - db) * (-LPWM) / 255.0f));
  }
  if (RPWM != 0) {
    int db = cfg.dead_band_right;
    RPWM = (RPWM > 0)
      ? db + (int)((255 - db) * RPWM / 255.0f)
      : -(db + (int)((255 - db) * (-RPWM) / 255.0f));
  }

#if MOTOR_DRIVER_TYPE == 0
  // ── PWM + DIR driver (default)
  // ← If your DIR logic is inverted, swap HIGH/LOW below.
  setMotorPWD_DIR(LPWM, MOTOR_L_DIR, 1);   // left
  setMotorPWD_DIR(RPWM, MOTOR_R_DIR, 2);   // right

#else
  // ── IN1 + IN2 driver (L298N style)
  // Redefine MOTOR_L_DIR as IN1 and MOTOR_R_DIR as IN2 in config.h,
  // and add IN2_L / IN2_R pin defines.
  // ← Uncomment and adapt the block below for L298N:
  //
  // if (LPWM > 0)      { digitalWrite(MOTOR_L_DIR, HIGH); }
  // else if (LPWM < 0) { digitalWrite(MOTOR_L_DIR, LOW);  }
  // else               { digitalWrite(MOTOR_L_DIR, LOW);  }
  // // MOTOR_L_IN2 is the second pin
  // setTimerPWM(1, abs(LPWM));
  // // mirror for right motor
#endif
}

// ─── INTERNAL: set direction and PWM for one motor ───────────────
static void setMotorPWD_DIR(int pwm, uint8_t dir_pin, uint8_t channel) {
  if (pwm > 0) {
    digitalWrite(dir_pin, LOW);   // ← forward
  } else if (pwm < 0) {
    digitalWrite(dir_pin, HIGH);  // ← reverse
  } else {
    // Coast mode: both direction signals off, PWM = 0
    // For active braking: set dir_pin HIGH and drive PWM — not used by default
    digitalWrite(dir_pin, LOW);
  }
  setTimerPWM(channel, abs(pwm));
}

// ─── INTERNAL: write PWM duty to Timer1 CH1 or CH2 ──────────────
static void setTimerPWM(uint8_t channel, int value) {
  // Map 0–255 input to 0–tim1Period timer counts
  uint32_t duty = ((uint32_t)value * tim1Period) / 255UL;
  if (channel == 1) TIM1->CCR1 = duty;
  else              TIM1->CCR2 = duty;
}

// ═════════════════════════════════════════════════════════════════
// MOTOR WITH RAMP
// Applies acceleration ramp to prevent wheel slip on LiPo power.
// Call this from PID loop instead of motor() directly when ramping
// is desired. rampedLeft/rampedRight are globals tracking current state.
// ═════════════════════════════════════════════════════════════════
void motorRamped(int targetL, int targetR, uint32_t dt_us) {
  // dt_us = microseconds since last call (from micros() delta)
  // Step size = (255 / accel_ramp_ms) per millisecond
  float step = (255.0f / activeP->accel_ramp_ms) * (dt_us / 1000.0f);

  rampedLeft  += constrain((float)targetL - rampedLeft,  -step, step);
  rampedRight += constrain((float)targetR - rampedRight, -step, step);

  motor((int)rampedLeft, (int)rampedRight);
}

void resetRamp() {
  rampedLeft  = 0.0f;
  rampedRight = 0.0f;
}

// ═════════════════════════════════════════════════════════════════
// DEAD BAND CALIBRATION
// Slowly ramps PWM up from 0 until the motor starts moving.
// User watches the motor and presses SELECT when it first spins.
// Result is saved to cfg.dead_band_left/right.
// ═════════════════════════════════════════════════════════════════
void calibrateDeadBand() {
  for (int side = 0; side < 2; side++) {
    const char* label = (side == 0) ? "LEFT  Motor" : "RIGHT Motor";
    int found_db = 0;

    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_7x14B);
      u8g.drawStr(5, 20, label);
      u8g.setFont(u8g_font_profont12);
      u8g.drawStr(5, 35, "Watch motor spin.");
      u8g.drawStr(5, 47, "Press SELECT when");
      u8g.drawStr(5, 59, "it just starts.");
    } while (u8g.nextPage());
    delay(2000);

    for (int pwm = 0; pwm <= 100; pwm++) {
      if (side == 0) { setTimerPWM(1, pwm); setTimerPWM(2, 0); }
      else           { setTimerPWM(1, 0);   setTimerPWM(2, pwm); }

      u8g.firstPage();
      do {
        u8g.setFont(u8g_font_7x14B);
        u8g.drawStr(5, 20, label);
        u8g.setFont(u8g_font_profont12);
        u8g.setPrintPos(5, 40); u8g.print(F("PWM: ")); u8g.print(pwm);
        u8g.drawStr(5, 55, "SELECT = it moved");
      } while (u8g.nextPage());

      if (digitalRead(BTN_SELECT) == LOW) {
        found_db = pwm;
        delay(300);
        break;
      }
      delay(80);
    }

    motor(0, 0);
    if (side == 0) cfg.dead_band_left  = found_db;
    else           cfg.dead_band_right = found_db;

    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_profont12);
      u8g.setPrintPos(10, 35);
      u8g.print(label);
      u8g.print(F(" DB="));
      u8g.print(found_db);
    } while (u8g.nextPage());
    delay(1500);
  }
  saveSettings();
}

// ═════════════════════════════════════════════════════════════════
// MOTOR TESTS  (called from menu)
// ═════════════════════════════════════════════════════════════════
static void motorTestScreen(const char* label) {
  u8g.firstPage();
  do {
    u8g.setFont(u8g_font_7x14B);
    u8g.drawStr(10, 30, label);
    u8g.setFont(u8g_font_profont12);
    u8g.drawStr(10, 50, "[BACK] to stop");
  } while (u8g.nextPage());
}

void motorTestForward() {
  motorTestScreen("FORWARD");
  motor(cfg.profiles[cfg.active_profile].base_speed,
        cfg.profiles[cfg.active_profile].base_speed);
  while (digitalRead(BTN_BACK) == HIGH);
  motor(0, 0); delay(200);
}

void motorTestReverse() {
  motorTestScreen("REVERSE");
  int spd = cfg.profiles[cfg.active_profile].base_speed;
  motor(-spd, -spd);
  while (digitalRead(BTN_BACK) == HIGH);
  motor(0, 0); delay(200);
}

void motorTestLeft() {
  motorTestScreen("LEFT MOTOR ONLY");
  motor(cfg.profiles[cfg.active_profile].base_speed, 0);
  while (digitalRead(BTN_BACK) == HIGH);
  motor(0, 0); delay(200);
}

void motorTestRight() {
  motorTestScreen("RIGHT MOTOR ONLY");
  motor(0, cfg.profiles[cfg.active_profile].base_speed);
  while (digitalRead(BTN_BACK) == HIGH);
  motor(0, 0); delay(200);
}

void motorTestSpinLeft() {
  motorTestScreen("SPIN LEFT");
  int spd = cfg.profiles[cfg.active_profile].base_speed;
  motor(-spd, spd);
  while (digitalRead(BTN_BACK) == HIGH);
  motor(0, 0); delay(200);
}

void motorTestSpinRight() {
  motorTestScreen("SPIN RIGHT");
  int spd = cfg.profiles[cfg.active_profile].base_speed;
  motor(spd, -spd);
  while (digitalRead(BTN_BACK) == HIGH);
  motor(0, 0); delay(200);
}

// PWM Sweep — ramps 0→255→0, both motors. Tests driver linearity.
void motorTestPWMSweep() {
  for (int pwm = 0; pwm <= 255; pwm += 3) {
    motor(pwm, pwm);
    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_7x14B);
      u8g.drawStr(10, 25, "PWM SWEEP");
      u8g.setFont(u8g_font_profont12);
      u8g.setPrintPos(10, 45); u8g.print(F("PWM: ")); u8g.print(pwm);
      // Draw progress bar
      u8g.drawFrame(5, 53, 118, 8);
      u8g.drawBox(6, 54, (int)(pwm / 2.17f), 6);
    } while (u8g.nextPage());
    delay(30);
    if (digitalRead(BTN_BACK) == LOW) { motor(0,0); return; }
  }
  for (int pwm = 255; pwm >= 0; pwm -= 3) {
    motor(pwm, pwm);
    delay(30);
    if (digitalRead(BTN_BACK) == LOW) break;
  }
  motor(0, 0);
}
