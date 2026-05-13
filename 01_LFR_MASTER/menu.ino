// ╔══════════════════════════════════════════════════════════════════╗
// ║  menu.ino  —  Full Hierarchical Menu System                     ║
// ║  8 top-level items · Nested submenus · Value editor            ║
// ║  Hold-to-fast-scroll · BACK = cancel · SELECT = confirm        ║
// ╚══════════════════════════════════════════════════════════════════╝

// ─── SHARED MENU DRAWING PRIMITIVES ──────────────────────────────

// Draw a 3-visible-item scrolling menu with highlight outline.
// items[]: array of string labels, count: total items, sel: selected idx.
// Returns the new selection after button handling (one press per call).
static void drawMenuList(const char* title, const char* items[],
                         int count, int sel) {
  int prev = (sel - 1 + count) % count;
  int next = (sel + 1) % count;

  u8g.firstPage();
  do {
    // Title bar
    u8g.setFont(u8g_font_profont12);
    u8g.drawStr(0, 10, title);
    drawBatteryIcon(110, 0);
    u8g.drawLine(0, 11, 127, 11);

    // Selection outline bitmap
    u8g.drawBitmapP(0, 22, 16, 16, bitmap_item_sel_outline);

    // Previous item (above, dimmer font)
    u8g.setFont(u8g_font_profont12);
    u8g.drawStr(8, 22, items[prev]);

    // Selected item (bold)
    u8g.setFont(u8g_font_7x14B);
    u8g.drawStr(8, 37, items[sel]);

    // Next item (below)
    u8g.setFont(u8g_font_profont12);
    u8g.drawStr(8, 52, items[next]);

    // Scrollbar
    u8g.drawBitmapP(122, 0, 1, 64, bitmap_scrollbar_bg);
    int bar_y = (int)((float)sel / count * 60);
    int bar_h = max(4, 60 / count);
    u8g.drawBox(123, bar_y, 4, bar_h);

    // [BCK] hint bottom-left (tiny)
    u8g.setFont(u8g_font_04b_03);
    u8g.drawStr(0, 63, "[BCK]=back");
  } while (u8g.nextPage());
}

// Navigate a simple menu. Returns selected index when SELECT pressed.
// Returns -1 when BACK pressed.
static int runMenuList(const char* title, const char* items[],
                       int count, int* selPtr) {
  while (1) {
    drawMenuList(title, items, count, *selPtr);

    ButtonEvent e = pollButtons();
    delay(20);   // brief yield — prevents hammering the button state machine

    switch (e) {
      case BTN_UP_SHORT:
        *selPtr = (*selPtr - 1 + count) % count; break;
      case BTN_DOWN_SHORT:
        *selPtr = (*selPtr + 1) % count; break;
      case BTN_UP_HOLD:
        *selPtr = (*selPtr - 1 + count) % count;
        delay(BTN_HOLD_FAST_MS); break;
      case BTN_DOWN_HOLD:
        *selPtr = (*selPtr + 1) % count;
        delay(BTN_HOLD_FAST_MS); break;
      case BTN_SELECT_SHORT:
        return *selPtr;
      case BTN_BACK_SHORT:
      case BTN_BACK_LONG:
        return -1;
      default: break;
    }
  }
}

// ═════════════════════════════════════════════════════════════════
// GENERIC VALUE EDITOR
// Displays current value, UP/DOWN to change, hold for fast scroll.
// SELECT confirms and saves. BACK cancels (returns original value).
// stepFine = small step (single press), stepCoarse = fast-scroll step.
// ═════════════════════════════════════════════════════════════════
float editFloat(const char* label, float value,
                float minV, float maxV,
                float stepFine, float stepCoarse) {
  float original = value;
  uint32_t holdStart = 0;
  bool holding = false;

  while (1) {
    // Draw editor screen
    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_7x14B);
      u8g.drawStr(5, 14, label);
      u8g.drawLine(0, 16, 127, 16);

      // Value box
      u8g.drawFrame(20, 25, 88, 20);
      u8g.setFont(u8g_font_10x20);
      char buf[16];
      dtostrf(value, 7, 2, buf);
      u8g.drawStr(24, 43, buf);

      u8g.setFont(u8g_font_04b_03);
      u8g.drawStr(0, 56, "[UP/DN]=change");
      u8g.drawStr(0, 63, "[SEL]=save [BCK]=cancel");

      // Range hint
      u8g.setPrintPos(70, 56);
      u8g.print(F("Hold=x10"));
    } while (u8g.nextPage());

    // Button handling with hold-to-fast-scroll
    bool up   = (digitalRead(BTN_UP)   == LOW);
    bool down = (digitalRead(BTN_DOWN) == LOW);
    bool sel  = (digitalRead(BTN_SELECT) == LOW);
    bool back = (digitalRead(BTN_BACK)   == LOW);

    if (sel)  { delay(200); return value; }
    if (back) { delay(200); return original; }

    if (up || down) {
      if (!holding) { holdStart = millis(); holding = true; }
      uint32_t held = millis() - holdStart;
      float step = (held > BTN_HOLD_INITIAL_MS) ? stepCoarse : stepFine;
      value += (up ? step : -step);
      value  = constrain(value, minV, maxV);
      delay((held > BTN_HOLD_INITIAL_MS) ? BTN_HOLD_FAST_MS : 200);
    } else {
      holding = false;
    }
  }
}

int editInt(const char* label, int value,
            int minV, int maxV,
            int stepFine, int stepCoarse) {
  int original = value;
  uint32_t holdStart = 0;
  bool holding = false;

  while (1) {
    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_7x14B);
      u8g.drawStr(5, 14, label);
      u8g.drawLine(0, 16, 127, 16);

      u8g.drawFrame(20, 25, 88, 20);
      u8g.setFont(u8g_font_10x20);
      u8g.setPrintPos(30, 43);
      u8g.print(value);

      u8g.setFont(u8g_font_04b_03);
      u8g.drawStr(0, 56, "[UP/DN]=change  Hold=fast");
      u8g.drawStr(0, 63, "[SEL]=save  [BCK]=cancel");
    } while (u8g.nextPage());

    bool up   = (digitalRead(BTN_UP)   == LOW);
    bool down = (digitalRead(BTN_DOWN) == LOW);
    bool sel  = (digitalRead(BTN_SELECT) == LOW);
    bool back = (digitalRead(BTN_BACK)   == LOW);

    if (sel)  { delay(200); return value; }
    if (back) { delay(200); return original; }

    if (up || down) {
      if (!holding) { holdStart = millis(); holding = true; }
      uint32_t held = millis() - holdStart;
      int step = (held > BTN_HOLD_INITIAL_MS) ? stepCoarse : stepFine;
      value += (up ? step : -step);
      value  = constrain(value, minV, maxV);
      delay((held > BTN_HOLD_INITIAL_MS) ? BTN_HOLD_FAST_MS : 200);
    } else {
      holding = false;
    }
  }
}

// Toggle between two states — returns new state
bool editBool(const char* label, bool value,
              const char* offLabel, const char* onLabel) {
  while (1) {
    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_7x14B);
      u8g.drawStr(5, 14, label);
      u8g.drawLine(0, 16, 127, 16);
      u8g.setFont(u8g_font_10x20);
      u8g.drawStr(20, 42, value ? onLabel : offLabel);
      u8g.setFont(u8g_font_04b_03);
      u8g.drawStr(0, 63, "[UP/DN/SEL]=toggle  [BCK]=cancel");
    } while (u8g.nextPage());

    ButtonEvent e = pollButtons();
    delay(30);

    if (e == BTN_UP_SHORT || e == BTN_DOWN_SHORT || e == BTN_SELECT_SHORT) {
      value = !value;
    }
    if (e == BTN_BACK_SHORT) return !value;  // cancel = return original
    delay(200);
  }
}

// ═════════════════════════════════════════════════════════════════
// ROOT MENU  — 8 top-level items
// ═════════════════════════════════════════════════════════════════
void menuRoot() {
  static const char* items[] = {
    "1. Line Follow",
    "2. Calibration",
    "3. PID Tuning",
    "4. Drive Modes",
    "5. Sensor Test",
    "6. Motor Test",
    "7. Settings",
    "8. Competition",
    "9. Profiles",
    "10. System Info"
  };
  const int COUNT = 10;

  while (1) {
    int result = runMenuList("=== LFR MASTER ===", items, COUNT, &menuSel[0]);
    if (result < 0) return;  // BACK at root does nothing, re-loop
    switch (result) {
      case 0: menuLineFollow();    break;
      case 1: menuCalibration();   break;
      case 2: menuPIDTuning();     break;
      case 3: menuDriveModes();    break;
      case 4: menuSensorTest();    break;
      case 5: menuMotorTest();     break;
      case 6: menuSettings();      break;
      case 7: competitionMode();   break;
      case 8: menuProfiles();      break;
      case 9: showSystemInfo();    break;
    }
  }
}

// ═════════════════════════════════════════════════════════════════
// 1. LINE FOLLOW SUBMENU
// ═════════════════════════════════════════════════════════════════
void menuLineFollow() {
  static const char* items[] = {
    "Standard",
    "Center Mode",
    "Left Edge",
    "Right Edge",
    "Turbo Mode"
  };
  const int COUNT = 5;

  while (1) {
    int r = runMenuList("LINE FOLLOW", items, COUNT, &menuSel[1]);
    if (r < 0) return;
    runLineFollow((FollowMode)r);
  }
}

// ═════════════════════════════════════════════════════════════════
// 2. CALIBRATION SUBMENU
// ═════════════════════════════════════════════════════════════════
void menuCalibration() {
  static const char* items[] = {
    "Auto Calibrate",
    "Manual Calibrate",
    "View Values",
    "Verify Calib",
    "Reset Calib"
  };
  const int COUNT = 5;

  while (1) {
    int r = runMenuList("CALIBRATION", items, COUNT, &menuSel[2]);
    if (r < 0) return;
    switch (r) {
      case 0: autoCalibrate();         break;
      case 1: manualCalibrate();       break;
      case 2: showCalibrationValues(); break;
      case 3: verifyCalibration();     break;
      case 4: resetCalibration();      break;
    }
  }
}

// ═════════════════════════════════════════════════════════════════
// 3. PID TUNING SUBMENU
// ═════════════════════════════════════════════════════════════════
void menuPIDTuning() {
  static const char* items[] = {
    "Kp",
    "Ki",
    "Kd",
    "Base Speed",
    "Max Speed",
    "Turbo Mult",
    "Deriv Alpha",
    "Integral Limit",
    "Accel Ramp (ms)",
    "Save to EEPROM",
    "Load EEPROM"
  };
  const int COUNT = 11;

  while (1) {
    int r = runMenuList("PID TUNING", items, COUNT, &menuSel[3]);
    if (r < 0) return;

    PIDProfile* P = &cfg.profiles[cfg.active_profile];

    switch (r) {
      case 0: P->kp            = editFloat("Kp",           P->kp,            0, 50,    0.5f, 5.0f); break;
      case 1: P->ki            = editFloat("Ki",           P->ki,            0, 5,     0.01f,0.1f); break;
      case 2: P->kd            = editFloat("Kd",           P->kd,            0, 2000,  10.0f,100.f); break;
      case 3: P->base_speed    = editInt  ("Base Speed",   P->base_speed,    0, 255,   5,    20);    break;
      case 4: P->max_speed     = editInt  ("Max Speed",    P->max_speed,     0, 255,   5,    20);    break;
      case 5: P->turbo_mult    = editFloat("Turbo Mult",   P->turbo_mult,    1.0f,2.0f,0.05f,0.2f); break;
      case 6: P->deriv_alpha   = editFloat("Deriv Alpha",  P->deriv_alpha,   0.05f,1.0f,0.05f,0.1f);break;
      case 7: P->integral_max  = editFloat("Integral Lim", P->integral_max,  0, 200,   5.0f, 20.f); break;
      case 8: P->accel_ramp_ms = editInt  ("Accel Ramp",   P->accel_ramp_ms, 0, 500,   5,    25);   break;
      case 9:
        saveSettings();
        showConfirm("Saved to EEPROM!");
        break;
      case 10:
        loadSettings();
        activeP = &cfg.profiles[cfg.active_profile];
        showConfirm("Loaded from EEPROM");
        break;
    }
    // Keep activeP pointer current after edit
    activeP = &cfg.profiles[cfg.active_profile];
  }
}

// ═════════════════════════════════════════════════════════════════
// 4. DRIVE MODES SUBMENU
// ═════════════════════════════════════════════════════════════════
void menuDriveModes() {
  static const char* items[] = {
    "Left Edge Offset",
    "Right Edge Offset",
    "Lost Line Mode",
    "Bridge Hold (ms)",
    "Spin Timeout (ms)",
    "Crossing Mode",
    "Cross Transit (ms)",
    "Look-ahead",
  };
  const int COUNT = 8;

  while (1) {
    int r = runMenuList("DRIVE MODES", items, COUNT, &menuSel[4]);
    if (r < 0) { saveSettings(); return; }

    static const char* lostModes[] = {"Bridge", "Spin", "Stop"};
    static const char* crossModes[] = {"Straight", "Sequence", "Left", "Right"};

    switch (r) {
      case 0: cfg.left_edge_offset   = editInt("Left Offset×10", cfg.left_edge_offset,  0, 500, 10, 50); break;
      case 1: cfg.right_edge_offset  = editInt("Right Offset×10",cfg.right_edge_offset, 0, 500, 10, 50); break;
      case 2: cfg.lost_mode          = editInt("Lost Line Mode",  cfg.lost_mode,         0, 2,   1,  1);  break;
      case 3: cfg.bridge_hold_ms     = editInt("Bridge Hold ms",  cfg.bridge_hold_ms,    0, 500, 5,  20); break;
      case 4: cfg.spin_timeout_ms    = editInt("Spin Timeout ms", cfg.spin_timeout_ms,   100,5000,50,200);break;
      case 5: cfg.crossing_action    = editInt("Crossing Mode",   cfg.crossing_action,   0, 3,   1,  1);  break;
      case 6: cfg.crossing_transit_ms= editInt("Cross Transit ms",cfg.crossing_transit_ms,50,1000,10,50); break;
      case 7: cfg.lookahead_en       = editBool("Look-ahead",  cfg.lookahead_en, "OFF", "ON"); break;
    }
  }
}

// ═════════════════════════════════════════════════════════════════
// 5. SENSOR TEST SUBMENU
// ═════════════════════════════════════════════════════════════════
void menuSensorTest() {
  static const char* items[] = {
    "Analog View",
    "Digital View",
    "Position View",
    "Noise Test",
    "Sensor Health"
  };
  const int COUNT = 5;

  while (1) {
    int r = runMenuList("SENSOR TEST", items, COUNT, &menuSel[5]);
    if (r < 0) return;
    switch (r) {
      case 0: analogView();    break;
      case 1: digitalView();   break;
      case 2: positionView();  break;
      case 3: noiseTest();     break;
      case 4: sensorHealth();  break;
    }
  }
}

// ═════════════════════════════════════════════════════════════════
// 6. MOTOR TEST SUBMENU
// ═════════════════════════════════════════════════════════════════
void menuMotorTest() {
  static const char* items[] = {
    "Forward",
    "Reverse",
    "Left Motor Only",
    "Right Motor Only",
    "Spin Left",
    "Spin Right",
    "PWM Sweep",
    "Dead Band Cal"
  };
  const int COUNT = 8;

  while (1) {
    int r = runMenuList("MOTOR TEST", items, COUNT, &menuSel[6]);
    if (r < 0) return;
    switch (r) {
      case 0: motorTestForward();    break;
      case 1: motorTestReverse();    break;
      case 2: motorTestLeft();       break;
      case 3: motorTestRight();      break;
      case 4: motorTestSpinLeft();   break;
      case 5: motorTestSpinRight();  break;
      case 6: motorTestPWMSweep();   break;
      case 7: calibrateDeadBand();   break;
    }
  }
}

// ═════════════════════════════════════════════════════════════════
// 7. SETTINGS SUBMENU
// ═════════════════════════════════════════════════════════════════
void menuSettings() {
  static const char* items[] = {
    "CPU Speed",
    "Line Mode",
    "ADC Resolution",
    "Serial Baud",
    "Telemetry",
    "OLED During Run",
    "Start Delay",
    "Battery Cutoff",
    "Dead Band L",
    "Dead Band R",
    "Factory Reset"
  };
  const int COUNT = 11;

  static const char* cpuLabels[]    = {"72 MHz", "96 MHz", "128 MHz"};
  static const char* lineModes[]    = {"Black/White", "White/Black"};
  static const char* adcBits[]      = {"8-bit", "10-bit", "12-bit"};
  static const char* telemModes[]   = {"OFF", "Error only", "Full PID"};
  static const char* oledModes[]    = {"Always ON", "Dim", "OFF (fast)"};
  static const char* delayOpts[]    = {"0s", "3s", "5s", "10s"};

  while (1) {
    int r = runMenuList("SETTINGS", items, COUNT, &menuSel[7]);
    if (r < 0) { saveSettings(); return; }

    switch (r) {
      case 0:
        cfg.cpu_speed_idx = editInt("CPU Speed Idx",   cfg.cpu_speed_idx,  0, 2, 1, 1);
        configureCPU(cfg.cpu_speed_idx);
        break;
      case 1:
        cfg.line_mode     = editInt("Line Mode",       cfg.line_mode,      0, 1, 1, 1);
        break;
      case 2: {
        int bits_idx = (cfg.adc_bits == 8) ? 0 : (cfg.adc_bits == 10) ? 1 : 2;
        bits_idx = editInt("ADC Res",  bits_idx, 0, 2, 1, 1);
        cfg.adc_bits = (bits_idx == 0) ? 8 : (bits_idx == 1) ? 10 : 12;
        analogReadResolution(cfg.adc_bits);
        break;
      }
      case 3: {
        static const long bauds[] = {9600, 57600, 115200, 230400, 460800};
        int bi = 2;  // default 115200
        for (int i = 0; i < 5; i++) if (bauds[i] == cfg.serial_baud) bi = i;
        bi = editInt("Baud Idx", bi, 0, 4, 1, 1);
        cfg.serial_baud = bauds[bi];
        Serial.end(); Serial.begin(cfg.serial_baud);
        break;
      }
      case 4: cfg.telemetry_mode  = editInt("Telemetry",    cfg.telemetry_mode,  0, 2, 1, 1); break;
      case 5: cfg.oled_dim_run    = editInt("OLED Run",     cfg.oled_dim_run,    0, 2, 1, 1); break;
      case 6: cfg.start_delay_sec = editInt("Start Delay s",cfg.start_delay_sec, 0, 10,1, 5); break;
      case 7: cfg.batt_cutoff_mv  = editInt("Cutoff (mV)",  cfg.batt_cutoff_mv,  5000,8000,50,200); break;
      case 8: cfg.dead_band_left  = editInt("Deadband L",   cfg.dead_band_left,  0, 100,1, 5); break;
      case 9: cfg.dead_band_right = editInt("Deadband R",   cfg.dead_band_right, 0, 100,1, 5); break;
      case 10:
        if (confirmDialog("Factory Reset?")) {
          factoryReset();
          activeP = &cfg.profiles[cfg.active_profile];
          showConfirm("Reset done. Reboot!");
        }
        break;
    }
    saveSettings();
  }
}

// ═════════════════════════════════════════════════════════════════
// 9. PROFILES SUBMENU
// ═════════════════════════════════════════════════════════════════
void menuProfiles() {
  static const char* items[] = {
    "Profile A: SAFE",
    "Profile B: NORM",
    "Profile C: TRBO",
    "Active Profile",
    "Copy A -> B",
    "Copy B -> C",
    "Copy C -> A"
  };
  const int COUNT = 7;

  while (1) {
    int r = runMenuList("PROFILES", items, COUNT, &menuSel[9]);
    if (r < 0) return;

    switch (r) {
      case 0: case 1: case 2:
        // Select this profile for editing in PID Tuning
        cfg.active_profile = r;
        activeP = &cfg.profiles[r];
        saveSettings();
        showConfirm("Profile selected.");
        menuPIDTuning();  // go directly to PID tuning for that profile
        break;
      case 3:
        cfg.active_profile = editInt("Active Profile", cfg.active_profile, 0, 2, 1, 1);
        activeP = &cfg.profiles[cfg.active_profile];
        saveSettings();
        break;
      case 4: memcpy(&cfg.profiles[1], &cfg.profiles[0], sizeof(PIDProfile)); saveSettings(); showConfirm("A copied to B"); break;
      case 5: memcpy(&cfg.profiles[2], &cfg.profiles[1], sizeof(PIDProfile)); saveSettings(); showConfirm("B copied to C"); break;
      case 6: memcpy(&cfg.profiles[0], &cfg.profiles[2], sizeof(PIDProfile)); saveSettings(); showConfirm("C copied to A"); break;
    }
  }
}

// ═════════════════════════════════════════════════════════════════
// UTILITY DIALOGS
// ═════════════════════════════════════════════════════════════════

// Show a brief "done" confirmation — auto-dismisses after 1.2s
void showConfirm(const char* msg) {
  u8g.firstPage();
  do {
    u8g.setFont(u8g_font_7x14B);
    u8g.drawStr(10, 35, msg);
  } while (u8g.nextPage());
  delay(1200);
}

// Yes/No confirmation dialog. Returns true if SELECT pressed.
bool confirmDialog(const char* question) {
  while (1) {
    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_7x14B);
      u8g.drawStr(5, 20, question);
      u8g.setFont(u8g_font_profont12);
      u8g.drawStr(10, 42, "[SELECT] = YES");
      u8g.drawStr(10, 55, "[BACK]   = NO");
    } while (u8g.nextPage());

    if (digitalRead(BTN_SELECT) == LOW) { delay(200); return true; }
    if (digitalRead(BTN_BACK)   == LOW) { delay(200); return false; }
  }
}
