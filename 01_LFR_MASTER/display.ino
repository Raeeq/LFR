// ╔══════════════════════════════════════════════════════════════════╗
// ║  display.ino  —  All OLED Drawing Functions                     ║
// ║                                                                 ║
// ║  Single source for every pixel drawn to the SSD1306.           ║
// ║  No drawing code lives anywhere else — keeps all other files    ║
// ║  clean and makes UI changes a one-file job.                    ║
// ║                                                                 ║
// ║  NOTE: drawBatteryIcon(), drawSensorBars(), drawRunningScreen() ║
// ║  are defined HERE. The copies scattered in pid.ino and          ║
// ║  sensor.ino during initial build should be removed to avoid     ║
// ║  duplicate definition errors. These are the authoritative       ║
// ║  versions with improvements.                                    ║
// ╚══════════════════════════════════════════════════════════════════╝

// ─── DISPLAY TIMING ──────────────────────────────────────────────
// OLED page render with U8glib takes ~6–10 ms per frame at 400 kHz I2C.
// During PID loop, we rate-limit OLED updates so the loop stays fast.
// ← Reduce OLED_RUN_INTERVAL_MS in pid.ino if you want more frequent
//   display updates (at the cost of slightly slower PID loop).

// ═════════════════════════════════════════════════════════════════
// SPLASH SCREEN — shown briefly on boot before self-test
// ═════════════════════════════════════════════════════════════════
void drawSplashScreen() {
  u8g.firstPage();
  do {
    // Large title
    u8g.setFont(u8g_font_7x14B);
    u8g.drawStr(10, 20, "LFR  MASTER");

    // Divider
    u8g.drawLine(0, 24, 127, 24);

    // Sub-info
    u8g.setFont(u8g_font_profont12);
    u8g.drawStr(5, 38, "14-Sensor · STM32");
    u8g.drawStr(5, 50, "PID · OC · Full Menu");

    // Version bottom-right
    u8g.setFont(u8g_font_04b_03);
    u8g.drawStr(70, 63, "v" FIRMWARE_VERSION);

    // CPU speed (confirmed after configureCPU runs)
    u8g.setPrintPos(0, 63);
    u8g.print(SystemCoreClock / 1000000);
    u8g.print(F("MHz"));
  } while (u8g.nextPage());
  delay(1800);
}

// ═════════════════════════════════════════════════════════════════
// BATTERY ICON  — 14×8 px, drawn top-right corner of every screen
// Proportional fill (6600mV=0%, 8400mV=100% for 2S LiPo).
// Blinks the outline when voltage is below BATT_WARN_MV.
// ← Change the mV range below if you use a different battery.
// ═════════════════════════════════════════════════════════════════
void drawBatteryIcon(uint8_t x, uint8_t y) {
  // Outer case (12×6 px) + terminal nub (2×4 px on right)
  u8g.drawFrame(x,      y + 1, 12, 6);   // case
  u8g.drawBox  (x + 12, y + 2,  2, 4);   // terminal nub

  // Fill level — map 6600–8400 mV to 0–10 px
  // ← If using 3S LiPo, change 6600→9900 and 8400→12600
  float pct = (batteryMV - 6600.0f) / (8400.0f - 6600.0f);
  pct = constrain(pct, 0.0f, 1.0f);
  int fill = (int)(pct * 10.0f);   // 10 px inner width
  if (fill > 0) u8g.drawBox(x + 1, y + 2, fill, 4);

  // Blink outline when low — toggle every 300 ms
  if (batteryLow && ((millis() / 300) & 1)) {
    u8g.setColorIndex(0);   // erase the outline (creates blink effect)
    u8g.drawFrame(x, y + 1, 12, 6);
    u8g.setColorIndex(1);
  }
}

// ═════════════════════════════════════════════════════════════════
// SENSOR BARS  — 14 vertical bars across the full OLED width
// Active sensor = filled rectangle, inactive = 2px stub at bottom.
// Used on the front page, position view, and PID run HUD.
//   x    = left edge pixel
//   y    = top pixel of bar area
//   barH = total bar height in pixels (fill height when active)
// ═════════════════════════════════════════════════════════════════
void drawSensorBars(uint8_t x, uint8_t y, uint8_t barH) {
  // 14 bars × (8px wide + 1px gap) = 126px — fits 128px screen ✓
  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    uint8_t bx = x + i * 9;
    u8g.drawFrame(bx, y, 8, barH);
    if (sensorDigital[i]) {
      u8g.drawBox(bx, y, 8, barH);        // active — solid fill
    } else {
      u8g.drawBox(bx, y + barH - 2, 8, 2); // inactive — stub only
    }
  }
}

// ═════════════════════════════════════════════════════════════════
// POSITION GAUGE  — horizontal line showing where the line is
// Maps line_position (10–140) to a horizontal bar.
// A vertical tick marks CENTER_POSITION.
//   x, y = top-left corner of gauge
//   w    = total gauge width in pixels
// ═════════════════════════════════════════════════════════════════
void drawPositionGauge(uint8_t x, uint8_t y, uint8_t w, float pos) {
  u8g.drawFrame(x, y, w, 7);

  // Center tick mark
  uint8_t cx = x + w / 2;
  u8g.drawLine(cx, y - 2, cx, y + 8);

  if (sumOnSensor > 0) {
    // Indicator dot — maps 10..140 onto 0..w
    int dot_x = x + (int)((pos - 10.0f) / 130.0f * (float)(w - 4)) + 2;
    dot_x = constrain(dot_x, (int)x + 1, (int)(x + w - 2));
    u8g.drawBox(dot_x - 2, y + 1, 4, 5);
  }
}

// ═════════════════════════════════════════════════════════════════
// ERROR GAUGE  — bidirectional bar, zero at centre
// Positive error → fill right of centre, negative → fill left.
//   cx = horizontal centre pixel of gauge
//   hw = half-width of gauge (so total width = hw × 2)
// ═════════════════════════════════════════════════════════════════
void drawErrorGauge(uint8_t cx, uint8_t y, uint8_t hw, float err) {
  u8g.drawFrame(cx - hw, y, hw * 2, 6);
  // Max error ≈ 65 (half of 130 weight range)
  int fill = (int)(err / 65.0f * hw);
  fill = constrain(fill, -(int)hw, (int)hw);
  if (fill >= 0) u8g.drawBox(cx,        y + 1, fill,  4);
  else           u8g.drawBox(cx + fill, y + 1, -fill, 4);
}

// ═════════════════════════════════════════════════════════════════
// STATUS BAR  — one-line summary shown at top of every run screen
// Contains: mode name · turbo flag · battery icon
// ═════════════════════════════════════════════════════════════════
void drawStatusBar(FollowMode mode) {
  u8g.setFont(u8g_font_profont12);

  // Mode name (left-aligned)
  static const char* modeNames[] = {
    "STANDARD", "CENTER", "LEFT EDGE", "RIGHT EDGE", "TURBO"
  };
  u8g.drawStr(0, 10, modeNames[(uint8_t)mode]);

  // TURBO indicator (centre)
  if (turbo_active) {
    u8g.setFont(u8g_font_04b_03);
    u8g.drawStr(62, 10, "TURBO");
  }

  // Battery icon (right)
  drawBatteryIcon(114, 1);

  // Horizontal divider
  u8g.drawLine(0, 11, 127, 11);
}

// ═════════════════════════════════════════════════════════════════
// RUNNING HUD  — full real-time screen during PID follow
// Updated at OLED_RUN_INTERVAL_MS rate (150ms) from pid.ino.
// Shows: status bar · sensor bars · error gauge · numerics
// ═════════════════════════════════════════════════════════════════
void drawRunningScreen(FollowMode mode, float err, float corr, int spd) {
  u8g.firstPage();
  do {
    // ── Row 1: status bar (0–11 px)
    drawStatusBar(mode);

    // ── Row 2: sensor bars (12–25 px, 13px tall)
    drawSensorBars(1, 13, 13);

    // ── Row 3: position gauge (27–33 px)
    if (sumOnSensor > 0) {
      drawPositionGauge(5, 27, 118, line_position);
    } else {
      u8g.setFont(u8g_font_04b_03);
      u8g.drawStr(45, 34, "LINE LOST");
    }

    // ── Row 4: error gauge (36–41 px)
    drawErrorGauge(64, 36, 55, err);

    // ── Row 5: numeric values (44–52 px)
    u8g.setFont(u8g_font_profont12);
    u8g.setPrintPos(0, 48);
    u8g.print(F("S:")); u8g.print(spd);
    u8g.print(F(" E:")); u8g.print(err, 1);
    u8g.print(F(" C:")); u8g.print(corr, 0);

    // ── Row 6: position + active sensor count + live offset (53–63 px)
    u8g.setPrintPos(0, 60);
    u8g.print(F("P:")); u8g.print(line_position, 1);
    u8g.print(F(" N:")); u8g.print(sumOnSensor);

    // Live speed offset indicator (right side)
    if (live_speed_offset != 0) {
      u8g.setPrintPos(95, 60);
      if (live_speed_offset > 0) u8g.print('+');
      u8g.print(live_speed_offset);
    }

    // [SEL]=Stop hint (bottom right, tiny)
    u8g.setFont(u8g_font_04b_03);
    u8g.drawStr(68, 63, "[SEL]=Stop [BCK]=Turbo");
  } while (u8g.nextPage());
}

// ═════════════════════════════════════════════════════════════════
// COUNTDOWN DISPLAY  — large numbers for pre-run countdown
// ═════════════════════════════════════════════════════════════════
void drawCountdown(int count) {
  u8g.firstPage();
  do {
    u8g.setFont(u8g_font_profont12);
    u8g.drawStr(15, 15, "GET READY...");

    // Large centre number
    u8g.setFont(u8g_font_10x20);
    char buf[4];
    itoa(count, buf, 10);
    // Centre the digit(s)
    uint8_t charW = (count >= 10) ? 20 : 10;
    u8g.drawStr((128 - charW) / 2, 50, buf);

    // Progress dots — one dot = one second remaining
    for (int d = 0; d < count && d < 10; d++) {
      u8g.drawBox(10 + d * 11, 56, 8, 6);
    }
  } while (u8g.nextPage());
}

// ═════════════════════════════════════════════════════════════════
// LOW BATTERY WARNING  — full-screen alert with LED flash
// Called when batteryMV drops below cfg.batt_cutoff_mv during run.
// ═════════════════════════════════════════════════════════════════
void drawLowBatteryWarning() {
  for (int flash = 0; flash < 6; flash++) {
    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_7x14B);
      u8g.drawStr(15, 22, "! LOW BATTERY !");
      u8g.drawLine(0, 26, 127, 26);
      u8g.setFont(u8g_font_profont12);
      u8g.setPrintPos(20, 42);
      u8g.print(batteryMV / 1000.0f, 2);
      u8g.print(F(" V"));
      u8g.drawStr(20, 56, "ROBOT STOPPED");
    } while (u8g.nextPage());
    digitalWrite(LED_PIN, LED_ON);  delay(180);
    digitalWrite(LED_PIN, LED_OFF); delay(180);
  }
}

// ═════════════════════════════════════════════════════════════════
// FRONT PAGE  — live sensor view on the idle home screen
// Called from loop() before the menu is opened.
// ═════════════════════════════════════════════════════════════════
void drawFrontPage() {
  u8g.firstPage();
  do {
    // Title
    u8g.setFont(u8g_font_7x14B);
    u8g.drawStr(18, 11, "LFR MASTER");
    drawBatteryIcon(112, 0);
    u8g.drawLine(0, 13, 127, 13);

    // Sensor bars — tall, centred in remaining space
    drawSensorBars(1, 15, 36);

    // Active profile name (below bars, left)
    u8g.setFont(u8g_font_profont12);
    u8g.setPrintPos(0, 62);
    u8g.print(activeP->name);
    u8g.print(F(" Kp:"));
    u8g.print(activeP->kp, 0);

    // Position readout (right side)
    u8g.setPrintPos(80, 62);
    if (sumOnSensor > 0) {
      u8g.print(F("P:"));
      u8g.print((float)weightedSum / sumOnSensor, 1);
    } else {
      u8g.print(F("---"));
    }

    // [DN]=Menu hint (bottom right)
    u8g.setFont(u8g_font_04b_03);
    u8g.drawStr(50, 63, "[DOWN]=Menu");
  } while (u8g.nextPage());
}

// ═════════════════════════════════════════════════════════════════
// PROFILE STATUS OVERLAY  — briefly shown when profile changes
// ═════════════════════════════════════════════════════════════════
void drawProfileBanner() {
  u8g.firstPage();
  do {
    u8g.setFont(u8g_font_7x14B);
    u8g.drawStr(10, 20, "PROFILE:");
    u8g.drawStr(10, 38, activeP->name);
    u8g.setFont(u8g_font_profont12);
    u8g.setPrintPos(10, 54);
    u8g.print(F("Kp="));  u8g.print(activeP->kp, 0);
    u8g.print(F(" Kd=")); u8g.print(activeP->kd, 0);
    u8g.print(F(" S="));  u8g.print(activeP->base_speed);
  } while (u8g.nextPage());
  delay(1200);
}

// ═════════════════════════════════════════════════════════════════
// SIMPLE TEXT SCREEN  — generic centred message + subtitle
// Used by motor tests and other quick-display situations.
// ═════════════════════════════════════════════════════════════════
void drawTextScreen(const char* title, const char* subtitle) {
  u8g.firstPage();
  do {
    u8g.setFont(u8g_font_7x14B);
    // Centre the title
    uint8_t tw = strlen(title) * 7;
    u8g.drawStr((128 - tw) / 2, 28, title);

    if (subtitle && strlen(subtitle) > 0) {
      u8g.setFont(u8g_font_profont12);
      uint8_t sw = strlen(subtitle) * 6;
      u8g.drawStr((128 - sw) / 2, 46, subtitle);
    }
  } while (u8g.nextPage());
}

// ═════════════════════════════════════════════════════════════════
// PROGRESS BAR  — generic horizontal progress indicator
// val: current value, maxVal: maximum value
// Used during calibration sweep and PWM sweep test.
// ═════════════════════════════════════════════════════════════════
void drawProgressBar(const char* label, int val, int maxVal) {
  u8g.firstPage();
  do {
    u8g.setFont(u8g_font_7x14B);
    u8g.drawStr(5, 25, label);

    // Percentage text
    u8g.setFont(u8g_font_profont12);
    u8g.setPrintPos(50, 42);
    u8g.print((int)(100.0f * val / maxVal));
    u8g.print(F("%"));

    // Bar
    u8g.drawFrame(5, 50, 118, 10);
    int fill = (int)(118.0f * val / maxVal);
    if (fill > 0) u8g.drawBox(6, 51, fill, 8);
  } while (u8g.nextPage());
}
