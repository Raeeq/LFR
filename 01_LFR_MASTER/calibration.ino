// ╔══════════════════════════════════════════════════════════════════╗
// ║  calibration.ino  —  Sensor Calibration & EEPROM Persistence   ║
// ║  Auto sweep · Manual snapshot · 12-bit EEPROM · Verify screen  ║
// ╚══════════════════════════════════════════════════════════════════╝

// ─── EEPROM HELPERS  (16-bit, little-endian) ─────────────────────
// Using manual byte writes instead of EEPROM.put<int16> to guarantee
// 2-byte storage regardless of how the STM32 core defines int size.
static void eepromWrite16(int addr, uint16_t val) {
  EEPROM.write(addr,     (uint8_t)(val & 0xFF));
  EEPROM.write(addr + 1, (uint8_t)(val >> 8));
}
static uint16_t eepromRead16(int addr) {
  return (uint16_t)EEPROM.read(addr)
       | ((uint16_t)EEPROM.read(addr + 1) << 8);
}

// ═════════════════════════════════════════════════════════════════
// SAVE CALIBRATION  — full 12-bit values, 2 bytes each
// Layout defined in config.h: EEPROM_CALIB_REF/MAX/MIN_ADDR
// ═════════════════════════════════════════════════════════════════
void saveCalibration() {
  for (int i = 0; i < SENSOR_COUNT; i++) {
    eepromWrite16(EEPROM_CALIB_REF_ADDR + i * 2, (uint16_t)Reference_ADC[i]);
    eepromWrite16(EEPROM_CALIB_MAX_ADDR + i * 2, (uint16_t)Max_ADC[i]);
    eepromWrite16(EEPROM_CALIB_MIN_ADDR + i * 2, (uint16_t)Min_ADC[i]);
  }
  Serial.println(F("Calibration saved to EEPROM."));
}

// ═════════════════════════════════════════════════════════════════
// LOAD CALIBRATION  — called from setup()
// Guards against uninitialised EEPROM (0xFFFF after first flash).
// ═════════════════════════════════════════════════════════════════
void loadCalibration() {
  bool any_invalid = false;
  for (int i = 0; i < SENSOR_COUNT; i++) {
    Reference_ADC[i] = (int)eepromRead16(EEPROM_CALIB_REF_ADDR + i * 2);
    Max_ADC[i]       = (int)eepromRead16(EEPROM_CALIB_MAX_ADDR + i * 2);
    Min_ADC[i]       = (int)eepromRead16(EEPROM_CALIB_MIN_ADDR + i * 2);

    // Sanitise — fresh EEPROM reads as 0xFFFF (65535)
    if (Max_ADC[i] > ADC_MAX) { Max_ADC[i]       = ADC_MAX / 2; any_invalid = true; }
    if (Min_ADC[i] > ADC_MAX) { Min_ADC[i]       = ADC_MAX / 2; any_invalid = true; }
    if (Reference_ADC[i] > ADC_MAX) { Reference_ADC[i] = ADC_MAX / 2; any_invalid = true; }
  }

  if (any_invalid) {
    Serial.println(F("WARNING: Calibration not found in EEPROM. Run Calibration first!"));
  } else {
    Serial.println(F("Calibration loaded OK."));
    printCalibrationTable();
  }
}

// ═════════════════════════════════════════════════════════════════
// AUTO CALIBRATION
// Robot sweeps left→right→right→left while sampling all sensors.
// Finds Min and Max ADC per channel. Reference = midpoint.
// Motor speed during sweep = 40% of base_speed.
// ═════════════════════════════════════════════════════════════════
void autoCalibrate() {
  // ── Pre-calibration screen
  u8g.firstPage();
  do {
    u8g.setFont(u8g_font_7x14B);
    u8g.drawStr(5, 15, "AUTO CALIBRATE");
    u8g.setFont(u8g_font_profont12);
    u8g.drawStr(5, 30, "Place on line.");
    u8g.drawStr(5, 42, "Robot will sweep");
    u8g.drawStr(5, 54, "left/right.");
    u8g.drawStr(5, 63, "[SEL]=Start [BCK]=Cancel");
  } while (u8g.nextPage());

  // Wait for SELECT or BACK
  while (1) {
    if (digitalRead(BTN_SELECT) == LOW) { delay(200); break; }
    if (digitalRead(BTN_BACK)   == LOW) { delay(200); return; }
  }

  // ── Initialise bounds to extremes
  for (int i = 0; i < SENSOR_COUNT; i++) {
    Max_ADC[i] = 0;
    Min_ADC[i] = ADC_MAX;
  }

  // ── Four-step sweep: L, R, R, L  (covers full sensor width twice)
  const int8_t sweepDir[4][2] = {
    {-1,  1},   // spin left
    { 1, -1},   // spin right
    { 1, -1},   // spin right again
    {-1,  1}    // spin left again
  };

  int sweepSpeed = (int)(activeP->base_speed * 0.40f);
  if (sweepSpeed < 40) sweepSpeed = 40;

  for (int step = 0; step < 4; step++) {
    // Show progress
    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_7x14B);
      u8g.drawStr(10, 25, "Calibrating...");
      u8g.setFont(u8g_font_profont12);
      u8g.setPrintPos(35, 45);
      u8g.print(F("Step "));
      u8g.print(step + 1);
      u8g.print(F("/4"));
      // Progress bar
      u8g.drawFrame(5, 53, 118, 8);
      u8g.drawBox(6, 54, (step + 1) * 29, 6);
    } while (u8g.nextPage());

    motor(sweepSpeed * sweepDir[step][0],
          sweepSpeed * sweepDir[step][1]);

    // Sample 600 times per step (each sample = 14 channels)
    for (int sweep = 0; sweep < 600; sweep++) {
      for (int i = 0; i < SENSOR_COUNT; i++) {
        selectChannel(i);
        delayMicroseconds(MUX_SETTLE_US);
        int v = analogRead(MUX_SIG);
        if (v > Max_ADC[i]) Max_ADC[i] = v;
        if (v < Min_ADC[i]) Min_ADC[i] = v;
      }
    }
    motor(0, 0);
    delay(250);   // pause before reversing
  }

  // ── Compute reference (midpoint)
  for (int i = 0; i < SENSOR_COUNT; i++) {
    Reference_ADC[i] = (Max_ADC[i] + Min_ADC[i]) / 2;
  }

  // ── Save to EEPROM
  saveCalibration();

  // ── Show results
  showCalibrationValues();
}

// ═════════════════════════════════════════════════════════════════
// MANUAL CALIBRATION
// Step 1: Place robot on white (off-line) → press SELECT → samples min
// Step 2: Place robot on black (on-line)  → press SELECT → samples max
// Reference = midpoint.
// Useful when the auto-sweep is not practical.
// ═════════════════════════════════════════════════════════════════
void manualCalibrate() {
  // ── Step 1: White surface
  u8g.firstPage();
  do {
    u8g.setFont(u8g_font_7x14B);
    u8g.drawStr(5, 15, "MANUAL CAL");
    u8g.setFont(u8g_font_profont12);
    u8g.drawStr(5, 30, "Step 1/2:");
    u8g.drawStr(5, 42, "Place on WHITE");
    u8g.drawStr(5, 54, "(off-line surface)");
    u8g.drawStr(5, 63, "[SEL]=Sample");
  } while (u8g.nextPage());

  while (digitalRead(BTN_SELECT) == HIGH) {
    if (digitalRead(BTN_BACK) == LOW) { delay(200); return; }
  }
  delay(200);

  // Sample 200 readings per channel, take minimum
  for (int i = 0; i < SENSOR_COUNT; i++) Min_ADC[i] = ADC_MAX;
  for (int s = 0; s < 200; s++) {
    for (int i = 0; i < SENSOR_COUNT; i++) {
      selectChannel(i);
      delayMicroseconds(MUX_SETTLE_US);
      int v = analogRead(MUX_SIG);
      if (v < Min_ADC[i]) Min_ADC[i] = v;
    }
  }

  // ── Step 2: Black line
  u8g.firstPage();
  do {
    u8g.setFont(u8g_font_7x14B);
    u8g.drawStr(5, 15, "MANUAL CAL");
    u8g.setFont(u8g_font_profont12);
    u8g.drawStr(5, 30, "Step 2/2:");
    u8g.drawStr(5, 42, "Place on BLACK");
    u8g.drawStr(5, 54, "(on-line surface)");
    u8g.drawStr(5, 63, "[SEL]=Sample");
  } while (u8g.nextPage());

  while (digitalRead(BTN_SELECT) == HIGH) {
    if (digitalRead(BTN_BACK) == LOW) { delay(200); return; }
  }
  delay(200);

  for (int i = 0; i < SENSOR_COUNT; i++) Max_ADC[i] = 0;
  for (int s = 0; s < 200; s++) {
    for (int i = 0; i < SENSOR_COUNT; i++) {
      selectChannel(i);
      delayMicroseconds(MUX_SETTLE_US);
      int v = analogRead(MUX_SIG);
      if (v > Max_ADC[i]) Max_ADC[i] = v;
    }
  }

  // ── Compute reference
  for (int i = 0; i < SENSOR_COUNT; i++) {
    Reference_ADC[i] = (Max_ADC[i] + Min_ADC[i]) / 2;
    // Sanity guard — if surfaces weren't distinct enough
    if (Max_ADC[i] - Min_ADC[i] < 200) {
      Reference_ADC[i] = ADC_MAX / 2;  // fall back to midpoint
    }
  }

  saveCalibration();
  showCalibrationValues();
}

// ═════════════════════════════════════════════════════════════════
// VERIFY CALIBRATION  — PASS/FAIL per sensor
// Checks: Min < Ref < Max  AND  range > 200 ADC counts
// ═════════════════════════════════════════════════════════════════
void verifyCalibration() {
  uint8_t passCount = 0;
  bool    pass[SENSOR_COUNT];

  for (int i = 0; i < SENSOR_COUNT; i++) {
    int range = Max_ADC[i] - Min_ADC[i];
    pass[i] = (Min_ADC[i] < Reference_ADC[i])
           && (Reference_ADC[i] < Max_ADC[i])
           && (range > 200);
    if (pass[i]) passCount++;
  }

  while (1) {
    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_7x14B);
      u8g.drawStr(10, 12, "CALIB VERIFY");
      u8g.setFont(u8g_font_profont12);

      u8g.setPrintPos(0, 24);
      u8g.print(passCount);
      u8g.print(F("/14 sensors OK"));

      // Two rows of 7 pass/fail indicators
      u8g.setFont(u8g_font_04b_03);
      for (uint8_t i = 0; i < 7; i++) {
        u8g.setPrintPos(i * 18, 36);
        u8g.print(i);
        u8g.setPrintPos(i * 18, 46);
        u8g.print(pass[i] ? F("OK") : F("!!"));
      }
      for (uint8_t i = 7; i < 14; i++) {
        u8g.setPrintPos((i - 7) * 18, 56);
        u8g.print(i);
        u8g.setPrintPos((i - 7) * 18, 64);
        u8g.print(pass[i] ? F("OK") : F("!!"));
      }
    } while (u8g.nextPage());

    if (digitalRead(BTN_BACK) == LOW) { delay(200); return; }
  }
}

// ═════════════════════════════════════════════════════════════════
// SHOW CALIBRATION VALUES on OLED
// Scrollable — UP/DOWN navigate through sensors
// ═════════════════════════════════════════════════════════════════
void showCalibrationValues() {
  int page = 0;  // 0 = sensors 0–6, 1 = sensors 7–13

  while (1) {
    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_profont12);
      u8g.drawStr(page == 0 ? "C0-C6 MIN|REF|MAX"
                            : "C7-C13 MIN|REF|MAX", 0, 10);

      u8g.setFont(u8g_font_04b_03);
      int start = page * 7;
      for (int i = start; i < start + 7 && i < SENSOR_COUNT; i++) {
        int row = 20 + (i - start) * 8;
        u8g.setPrintPos(0, row);
        u8g.print(F("C")); u8g.print(i); u8g.print(F(":"));
        u8g.setPrintPos(22, row); u8g.print(Min_ADC[i]);
        u8g.setPrintPos(52, row); u8g.print(Reference_ADC[i]);
        u8g.setPrintPos(82, row); u8g.print(Max_ADC[i]);
      }

      u8g.setFont(u8g_font_profont12);
      u8g.drawStr(0, 63, page == 0 ? "[DN]=next [BCK]=done"
                                   : "[UP]=prev [BCK]=done");
    } while (u8g.nextPage());

    if (digitalRead(BTN_DOWN)  == LOW) { page = 1; delay(200); }
    if (digitalRead(BTN_UP)    == LOW) { page = 0; delay(200); }
    if (digitalRead(BTN_BACK)  == LOW) { delay(200); return; }
  }
}

// ═════════════════════════════════════════════════════════════════
// RESET CALIBRATION  — wipes EEPROM calibration block
// ═════════════════════════════════════════════════════════════════
void resetCalibration() {
  // Confirmation prompt
  u8g.firstPage();
  do {
    u8g.setFont(u8g_font_7x14B);
    u8g.drawStr(5, 20, "ERASE CALIB?");
    u8g.setFont(u8g_font_profont12);
    u8g.drawStr(5, 38, "[SEL]=Yes, erase");
    u8g.drawStr(5, 50, "[BCK]=Cancel");
  } while (u8g.nextPage());

  while (1) {
    if (digitalRead(BTN_SELECT) == LOW) {
      // Fill calibration region with 0x00
      for (int addr = EEPROM_CALIB_REF_ADDR;
               addr < EEPROM_CALIB_MIN_ADDR + SENSOR_COUNT * 2;
               addr++) {
        EEPROM.write(addr, 0x00);
      }
      // Reset RAM arrays to safe midpoint
      for (int i = 0; i < SENSOR_COUNT; i++) {
        Reference_ADC[i] = ADC_MAX / 2;
        Max_ADC[i]       = ADC_MAX / 2;
        Min_ADC[i]       = ADC_MAX / 2;
      }
      u8g.firstPage();
      do {
        u8g.setFont(u8g_font_7x14B);
        u8g.drawStr(10, 35, "Calib cleared.");
      } while (u8g.nextPage());
      delay(1500);
      return;
    }
    if (digitalRead(BTN_BACK) == LOW) { delay(200); return; }
  }
}

// ─── SERIAL DEBUG ─────────────────────────────────────────────────
void printCalibrationTable() {
  Serial.println(F("=== Calibration Table ==="));
  Serial.println(F("Ch | Min  | Ref  | Max  | Range"));
  Serial.println(F("---+------+------+------+------"));
  for (int i = 0; i < SENSOR_COUNT; i++) {
    Serial.print(F("C")); Serial.print(i < 10 ? " " : ""); Serial.print(i);
    Serial.print(F(" | ")); Serial.print(Min_ADC[i]);
    Serial.print(F(" | ")); Serial.print(Reference_ADC[i]);
    Serial.print(F(" | ")); Serial.print(Max_ADC[i]);
    Serial.print(F(" | ")); Serial.println(Max_ADC[i] - Min_ADC[i]);
  }
}
