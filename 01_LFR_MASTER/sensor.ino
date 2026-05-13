// ╔══════════════════════════════════════════════════════════════════╗
// ║  sensor.ino  —  14-Channel Sensor Array                         ║
// ║  Direct register MUX · 12-bit ADC oversampling                 ║
// ║  Noise test · Analog/Digital/Position live views               ║
// ╚══════════════════════════════════════════════════════════════════╝

// ═════════════════════════════════════════════════════════════════
// SELECT MUX CHANNEL — direct register write (all 4 bits atomic)
// 4× faster than 4 separate digitalWrite() calls.
//
// MUX wiring: S0=PB15, S1=PB14, S2=PB13, S3=PB12
// Channel bit mapping to GPIOB bits:
//   ch bit0 (S0) → PB15    ch bit2 (S2) → PB13
//   ch bit1 (S1) → PB14    ch bit3 (S3) → PB12
// So PB[15:12] = [S0][S1][S2][S3] = reversed nibble of ch.
// ═════════════════════════════════════════════════════════════════
inline void selectChannel(uint8_t ch) {
  // Reverse the 4-bit nibble so bit0→PB15, bit3→PB12
  uint16_t rev = ((ch & 0x1) << 3)   // bit0 of ch → bit3 (PB12... wait)
               | ((ch & 0x2) << 1)
               | ((ch & 0x4) >> 1)
               | ((ch & 0x8) >> 3);
  // Write bits 12–15 of GPIOB atomically
  GPIOB->ODR = (GPIOB->ODR & ~(0xF << 12)) | (rev << 12);
}

// ═════════════════════════════════════════════════════════════════
// READ ALL 14 SENSORS
// Fills: sensorADC[], sensorDigital[], sumOnSensor, weightedSum,
//        bitSensor
// Hot path — called every PID loop iteration. Keep tight.
// ═════════════════════════════════════════════════════════════════
void readSensors() {
  sumOnSensor  = 0;
  weightedSum  = 0;
  bitSensor    = 0;

  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    selectChannel(i);
    delayMicroseconds(MUX_SETTLE_US);   // 5 µs — 25× more than CD74HC4067 needs

    // Oversampling: ADC_SAMPLES readings averaged → reduces ADC noise
    // At 128 MHz + 12-bit ADC: each analogRead ≈ 1–2 µs.
    // ADC_SAMPLES=3 adds ≈6 µs per channel, 84 µs total. Worth it.
    int32_t acc = 0;
    for (uint8_t s = 0; s < ADC_SAMPLES; s++) acc += analogRead(MUX_SIG);
    sensorADC[i] = (int)(acc / ADC_SAMPLES);

    // Threshold: above reference = on the line (for black-on-white)
    // cfg.line_mode == 1 inverts this for white-on-black tracks
    bool aboveRef = (sensorADC[i] > Reference_ADC[i]);
    sensorDigital[i] = (aboveRef ^ (bool)cfg.line_mode) ? 1 : 0;

    sumOnSensor += sensorDigital[i];
    weightedSum += sensorDigital[i] * WeightValue[i];
    // Binary pattern — C0=LSB, C13=bit13
    if (sensorDigital[i]) bitSensor |= bitWeight[i];
  }
}


// NOTE: drawSensorBars() is defined in display.ino — single source of truth.
// Calls from analogView(), digitalView(), positionView() below resolve there.

// ═════════════════════════════════════════════════════════════════
// SENSOR TEST SCREENS (called from menu)
// Each runs until BTN_BACK is pressed.
// ═════════════════════════════════════════════════════════════════

// ── 1. ANALOG VIEW — live 12-bit ADC value for all 14 channels ──
void analogView() {
  while (1) {
    readSensors();
    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_profont12);
      u8g.drawStr(25, 10, "ANALOG (ADC)");

      // Two rows of 7 channels each
      // Row 1: C0–C6
      for (uint8_t i = 0; i < 7; i++) {
        u8g.setPrintPos(i * 18, 28);
        // Print 4-digit ADC value, scaled to 0–999 for display space
        u8g.print(sensorADC[i] / 4);   // 0–4095 → 0–1023 (3 digits)
      }
      // Row labels C0–C6
      for (uint8_t i = 0; i < 7; i++) {
        u8g.setPrintPos(i * 18 + 2, 18);
        u8g.print(i);
      }

      // Row 2: C7–C13
      for (uint8_t i = 7; i < 14; i++) {
        u8g.setPrintPos((i - 7) * 18, 52);
        u8g.print(sensorADC[i] / 4);
      }
      for (uint8_t i = 7; i < 14; i++) {
        u8g.setPrintPos((i - 7) * 18 + 2, 42);
        u8g.print(i);
      }

      u8g.drawStr(28, 63, "[BACK]=exit");
    } while (u8g.nextPage());

    if (digitalRead(BTN_BACK) == LOW) { delay(200); return; }
  }
}

// ── 2. DIGITAL VIEW — binary 0/1 bar per sensor + bitSensor code ─
void digitalView() {
  while (1) {
    readSensors();
    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_profont12);
      u8g.drawStr(28, 10, "DIGITAL VIEW");

      // Draw bars
      drawSensorBars(1, 13, 30);

      // Channel labels below bars
      u8g.setFont(u8g_font_04b_03);  // tiny font for labels
      for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
        u8g.setPrintPos(1 + i * 9, 50);
        if (i < 10) u8g.print(i);
        else { u8g.print(i - 10); }  // abbreviate 10-13 as 0-3 with underline
      }

      u8g.setFont(u8g_font_profont12);
      // BitSensor pattern
      u8g.setPrintPos(0, 63);
      u8g.print(F("B:"));
      u8g.print(bitSensor);
      u8g.print(F(" N:"));
      u8g.print(sumOnSensor);
    } while (u8g.nextPage());

    if (digitalRead(BTN_BACK) == LOW) { delay(200); return; }
  }
}

// ── 3. POSITION VIEW — live line_position, error, sumOnSensor ────
void positionView() {
  while (1) {
    readSensors();
    float pos = (sumOnSensor > 0) ? (float)weightedSum / sumOnSensor : -1.0f;
    float err = CENTER_POSITION - pos;

    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_7x14B);
      u8g.drawStr(20, 12, "POSITION VIEW");

      // Sensor bars (compact)
      drawSensorBars(1, 14, 20);

      u8g.setFont(u8g_font_profont12);

      // Position gauge — horizontal bar showing pos within 10–140 range
      int gauge_w = 118;
      u8g.drawFrame(5, 37, gauge_w, 8);
      if (sumOnSensor > 0) {
        int fill = (int)((pos - 10.0f) / 130.0f * gauge_w);
        fill = constrain(fill, 0, gauge_w);
        u8g.drawBox(5, 37, fill, 8);
        // Center marker
        u8g.drawLine(5 + gauge_w / 2, 35, 5 + gauge_w / 2, 46);
      }

      u8g.setPrintPos(0, 55);
      u8g.print(F("Pos:"));
      if (sumOnSensor > 0) u8g.print(pos, 1); else u8g.print(F("LOST"));
      u8g.print(F("  Err:"));
      if (sumOnSensor > 0) u8g.print(err, 1); else u8g.print(F("---"));

      u8g.setPrintPos(0, 64);
      u8g.print(F("Active:"));
      u8g.print(sumOnSensor);
      u8g.print(F("/14"));
    } while (u8g.nextPage());

    if (digitalRead(BTN_BACK) == LOW) { delay(200); return; }
  }
}

// ── 4. NOISE TEST — 1000 samples per sensor, shows variance ──────
// High variance = electrical noise (usually from motor PWM coupling).
// Ideal variance < 5 ADC counts. > 30 = wiring problem.
void noiseTest() {
  u8g.firstPage();
  do {
    u8g.setFont(u8g_font_7x14B);
    u8g.drawStr(5, 30, "Testing noise...");
    u8g.setFont(u8g_font_profont12);
    u8g.drawStr(5, 48, "Keep robot still");
    u8g.drawStr(5, 60, "Motors will run!");
  } while (u8g.nextPage());
  delay(1500);

  // Run motors at base_speed during noise test — simulates real conditions
  motor(activeP->base_speed, activeP->base_speed);

  float variance[SENSOR_COUNT] = {0};
  float mean[SENSOR_COUNT]     = {0};
  const int SAMPLES = 500;

  // Pass 1: compute mean
  for (int s = 0; s < SAMPLES; s++) {
    for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
      selectChannel(i);
      delayMicroseconds(MUX_SETTLE_US);
      mean[i] += analogRead(MUX_SIG);
    }
  }
  for (uint8_t i = 0; i < SENSOR_COUNT; i++) mean[i] /= SAMPLES;

  // Pass 2: compute variance
  for (int s = 0; s < SAMPLES; s++) {
    for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
      selectChannel(i);
      delayMicroseconds(MUX_SETTLE_US);
      float diff = analogRead(MUX_SIG) - mean[i];
      variance[i] += diff * diff;
    }
  }
  for (uint8_t i = 0; i < SENSOR_COUNT; i++) variance[i] = sqrtf(variance[i] / SAMPLES);

  motor(0, 0);

  // Display results — two rows of 7
  while (1) {
    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_profont12);
      u8g.drawStr(20, 10, "NOISE (StdDev)");
      u8g.setFont(u8g_font_04b_03);

      // Row 1: C0–C6
      for (uint8_t i = 0; i < 7; i++) {
        u8g.setPrintPos(i * 18, 20);
        u8g.print(i);
        u8g.setPrintPos(i * 18, 32);
        u8g.print((int)variance[i]);
      }
      // Row 2: C7–C13
      for (uint8_t i = 7; i < 14; i++) {
        u8g.setPrintPos((i-7) * 18, 44);
        u8g.print(i);
        u8g.setPrintPos((i-7) * 18, 56);
        u8g.print((int)variance[i]);
      }

      u8g.setFont(u8g_font_profont12);
      u8g.drawStr(20, 63, "[BACK]=exit");
    } while (u8g.nextPage());

    // Also print to Serial for logging
    Serial.println(F("=== Noise Test (StdDev ADC counts) ==="));
    for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
      Serial.print(F("C")); Serial.print(i);
      Serial.print(F(": ")); Serial.print(variance[i], 1);
      Serial.print(variance[i] > 30 ? F(" !! HIGH") : F(" OK"));
      Serial.println();
    }

    if (digitalRead(BTN_BACK) == LOW) { delay(200); return; }
  }
}

// ── 5. SENSOR HEALTH — PASS/FAIL per channel based on calib range ─
void sensorHealth() {
  while (1) {
    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_profont12);
      u8g.drawStr(20, 10, "SENSOR HEALTH");
      u8g.setFont(u8g_font_04b_03);

      for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
        uint8_t col = (i < 7) ? (i * 18) : ((i - 7) * 18);
        uint8_t row = (i < 7) ? 24 : 48;
        int range = Max_ADC[i] - Min_ADC[i];
        bool pass = (Min_ADC[i] < Reference_ADC[i])
                 && (Reference_ADC[i] < Max_ADC[i])
                 && (range > 200);    // needs > 200 ADC counts of range
        u8g.setPrintPos(col, row - 10);
        u8g.print(i);
        u8g.setPrintPos(col, row);
        u8g.print(pass ? "OK" : "!!");
      }
      u8g.setFont(u8g_font_profont12);
      u8g.drawStr(20, 63, "[BACK]=exit");
    } while (u8g.nextPage());

    if (digitalRead(BTN_BACK) == LOW) { delay(200); return; }
  }
}
