// ╔══════════════════════════════════════════════════════════════════╗
// ║  system.ino  —  CPU Clock · Battery · EEPROM · Self-Test        ║
// ║               Competition Mode · Run Statistics                 ║
// ╚══════════════════════════════════════════════════════════════════╝

// ═════════════════════════════════════════════════════════════════
// CPU CLOCK CONFIGURATION
// Overclocks STM32F103 via direct RCC register manipulation.
// Called at the very top of setup() before any timing-dependent code.
// After this call, millis()/delay() are recalibrated via SysTick.
// ═════════════════════════════════════════════════════════════════
void configureCPU(uint8_t speed_idx) {
  // Step 1: Switch to internal HSI (8 MHz) while we reconfigure PLL
  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;
  while ((RCC->CFGR & RCC_CFGR_SWS) != 0x00);

  // Step 2: Disable PLL
  RCC->CR &= ~RCC_CR_PLLON;
  while (RCC->CR & RCC_CR_PLLRDY);

  // Step 3: Enable HSE (external 8 MHz crystal on Blue Pill)
  RCC->CR |= RCC_CR_HSEON;
  uint32_t timeout = 0;
  while (!(RCC->CR & RCC_CR_HSERDY) && ++timeout < 100000);
  if (!(RCC->CR & RCC_CR_HSERDY)) return;   // HSE failed — stay on HSI at ~8 MHz

  // Step 4: Set flash wait states (must be ≥ 2 for clocks above 48 MHz)
  FLASH->ACR = FLASH_ACR_LATENCY_2 | FLASH_ACR_PRFTBE;

  // Step 5: Select PLL multiplier and bus prescalers
  // APB1 max is 36 MHz — always divide down at high speeds.
  // ⚠ APB2 is over-spec'd at 128 MHz but has been stable in competition use.
  uint32_t pll_mul, apb1_pre;
  uint32_t target_hz;

  switch (speed_idx) {
    case CPU_96MHz:
      // HSE (8 MHz) × 12 = 96 MHz
      pll_mul  = RCC_CFGR_PLLMULL12;   // ← 96 MHz
      apb1_pre = RCC_CFGR_PPRE1_DIV4;  // APB1 = 24 MHz (below 36 MHz ✓)
      target_hz = 96000000UL;
      break;
    case CPU_128MHz:
      // HSE (8 MHz) × 16 = 128 MHz  ← OVERCLOCKED beyond spec
      // ⚠ Some chips need RCC_CFGR_PLLXTPRE_HSE_DIV2 for stability.
      //   If random crashes occur at 128 MHz, try 96 MHz instead.
      pll_mul  = RCC_CFGR_PLLMULL16;   // ← 128 MHz OC
      apb1_pre = RCC_CFGR_PPRE1_DIV4;  // APB1 = 32 MHz (below 36 MHz ✓)
      target_hz = 128000000UL;
      break;
    default: // CPU_72MHz — stock speed
      pll_mul  = RCC_CFGR_PLLMULL9;    // ← 72 MHz (spec max)
      apb1_pre = RCC_CFGR_PPRE1_DIV2;  // APB1 = 36 MHz (exactly at limit)
      target_hz = 72000000UL;
      break;
  }

  // Step 6: Write PLL config — HSE source + chosen multiplier + bus dividers
  RCC->CFGR = (RCC->CFGR
    & ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL | RCC_CFGR_PPRE1 | RCC_CFGR_HPRE))
    | RCC_CFGR_PLLSRC    // HSE → PLL
    | pll_mul
    | apb1_pre
    | RCC_CFGR_HPRE_DIV1 // AHB = full speed
    | RCC_CFGR_PPRE2_DIV1; // APB2 = full speed

  // Step 7: Re-enable PLL
  RCC->CR |= RCC_CR_PLLON;
  while (!(RCC->CR & RCC_CR_PLLRDY));

  // Step 8: Switch system clock to PLL
  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
  while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

  // Step 9: Update SystemCoreClock and recalibrate SysTick (fixes millis/delay)
  SystemCoreClock = target_hz;
  SysTick_Config(SystemCoreClock / 1000);   // 1 ms tick
}

// ═════════════════════════════════════════════════════════════════
// BATTERY MONITORING
// Call updateBattery(false) in main loop and PID loop.
// Call updateBattery(true) to force an immediate read.
// ═════════════════════════════════════════════════════════════════
void updateBattery(bool force) {
  uint32_t now = millis();
  if (!force && (now - lastBatteryRead < BATT_SAMPLE_INTERVAL_MS)) return;
  lastBatteryRead = now;

  // Average 8 ADC samples for a stable reading
  uint32_t acc = 0;
  for (int i = 0; i < 8; i++) acc += analogRead(BATTERY_PIN);
  float adc_avg = acc / 8.0f;

  // Convert ADC → millivolts
  // V_pin = (adc / ADC_MAX) × 3300 mV
  // V_batt = V_pin × divider_ratio
  // V_pin = (adc / 4095) * 3.3V,  V_batt = V_pin * divider_ratio
  batteryMV = (adc_avg / 4095.0f) * 3300.0f * cfg.batt_divider_ratio;

  batteryLow      = (batteryMV < BATT_WARN_MV);
  batteryCritical = (batteryMV < cfg.batt_cutoff_mv);

  // Track min voltage for post-run stats
  if (batteryMV < currentStats.min_battery_mv) {
    currentStats.min_battery_mv = batteryMV;
  }
}

// ═════════════════════════════════════════════════════════════════
// EEPROM — SETTINGS  (uses EEPROM.put/get — handles floats/structs)
// ═════════════════════════════════════════════════════════════════
void loadSettings() {
  uint16_t magic;
  EEPROM.get(EEPROM_MAGIC_ADDR, magic);

  if (magic != EEPROM_VERSION) {
    // First boot or layout changed — write factory defaults
    applyDefaultSettings();
    saveSettings();
    return;
  }
  EEPROM.get(EEPROM_SETTINGS_ADDR, cfg);

  // Guard: clamp any values that may have been corrupted
  cfg.active_profile   = constrain(cfg.active_profile, 0, 2);
  cfg.dead_band_left   = constrain(cfg.dead_band_left, 0, 80);
  cfg.dead_band_right  = constrain(cfg.dead_band_right, 0, 80);
  cfg.adc_bits         = constrain(cfg.adc_bits, 8, 12);
  if (cfg.serial_baud == 0) cfg.serial_baud = 115200;
}

void saveSettings() {
  cfg.magic = EEPROM_VERSION;
  EEPROM.put(EEPROM_MAGIC_ADDR, (uint16_t)EEPROM_VERSION);
  EEPROM.put(EEPROM_SETTINGS_ADDR, cfg);
  // STM32 Arduino EEPROM is emulated in flash.
  // Writes are committed automatically — no .commit() call needed.
}

void applyDefaultSettings() {
  cfg.magic             = EEPROM_VERSION;
  cfg.cpu_speed_idx     = DEFAULT_CPU_SPEED_IDX;
  cfg.line_mode         = LINE_BLACK_ON_WHITE;
  cfg.adc_bits          = 12;
  cfg.lost_mode         = LOST_BRIDGE;
  cfg.bridge_hold_ms    = DEFAULT_BRIDGE_HOLD_MS;
  cfg.spin_timeout_ms   = DEFAULT_SPIN_TIMEOUT_MS;
  cfg.crossing_thresh   = DEFAULT_CROSSING_THRESH;
  cfg.crossing_transit_ms = DEFAULT_CROSS_TRANSIT_MS;
  cfg.crossing_action   = CROSS_STRAIGHT;
  cfg.left_edge_offset  = 200;    // 20.0 weight units
  cfg.right_edge_offset = 200;
  cfg.lookahead_en      = 1;
  cfg.dead_band_left    = 25;     // ← Calibrate via Motor Test → Dead Band Test
  cfg.dead_band_right   = 25;
  cfg.batt_cutoff_mv    = BATT_CUTOFF_MV;
  cfg.batt_divider_ratio = BATT_DIVIDER_RATIO;
  cfg.oled_dim_run      = 0;
  cfg.telemetry_mode    = TELEM_OFF;
  cfg.serial_baud       = 115200;
  cfg.start_delay_sec   = 3;
  cfg.active_profile    = 0;

  // Profiles — member-by-member (C++ forbids aggregate-init of existing objects)
  // Profile A — SAFE
  cfg.profiles[0].kp            = PROF_A_KP;
  cfg.profiles[0].ki            = PROF_A_KI;
  cfg.profiles[0].kd            = PROF_A_KD;
  cfg.profiles[0].base_speed    = PROF_A_BASE_SPD;
  cfg.profiles[0].max_speed     = PROF_A_MAX_SPD;
  cfg.profiles[0].turbo_mult    = PROF_A_TURBO;
  cfg.profiles[0].deriv_alpha   = PROF_A_ALPHA;
  cfg.profiles[0].integral_max  = PROF_A_ILIMIT;
  cfg.profiles[0].accel_ramp_ms = PROF_A_RAMP_MS;
  strncpy(cfg.profiles[0].name, "SAFE ", 6);

  // Profile B — NORMAL
  cfg.profiles[1].kp            = PROF_B_KP;
  cfg.profiles[1].ki            = PROF_B_KI;
  cfg.profiles[1].kd            = PROF_B_KD;
  cfg.profiles[1].base_speed    = PROF_B_BASE_SPD;
  cfg.profiles[1].max_speed     = PROF_B_MAX_SPD;
  cfg.profiles[1].turbo_mult    = PROF_B_TURBO;
  cfg.profiles[1].deriv_alpha   = PROF_B_ALPHA;
  cfg.profiles[1].integral_max  = PROF_B_ILIMIT;
  cfg.profiles[1].accel_ramp_ms = PROF_B_RAMP_MS;
  strncpy(cfg.profiles[1].name, "NORM ", 6);

  // Profile C — TURBO
  cfg.profiles[2].kp            = PROF_C_KP;
  cfg.profiles[2].ki            = PROF_C_KI;
  cfg.profiles[2].kd            = PROF_C_KD;
  cfg.profiles[2].base_speed    = PROF_C_BASE_SPD;
  cfg.profiles[2].max_speed     = PROF_C_MAX_SPD;
  cfg.profiles[2].turbo_mult    = PROF_C_TURBO;
  cfg.profiles[2].deriv_alpha   = PROF_C_ALPHA;
  cfg.profiles[2].integral_max  = PROF_C_ILIMIT;
  cfg.profiles[2].accel_ramp_ms = PROF_C_RAMP_MS;
  strncpy(cfg.profiles[2].name, "TRBO ", 6);
}

void factoryReset() {
  // Wipe magic → next loadSettings() will rebuild defaults
  uint16_t zero = 0;
  EEPROM.put(EEPROM_MAGIC_ADDR, zero);
  applyDefaultSettings();
  saveSettings();
}

// ═════════════════════════════════════════════════════════════════
// BOOT SELF-TEST
// Runs on every power-on. Shows PASS/FAIL per subsystem on OLED.
// Robot should not be moving — motors tested briefly at low PWM.
// ═════════════════════════════════════════════════════════════════
void runSelfTest() {
  bool sensorOK   = true;
  bool eepromOK   = true;
  bool calibOK    = true;
  bool motorOK    = true;
  bool battOK     = true;
  uint8_t sensorFail = 0;

  // ── Sensors: read all 14 channels, flag stuck-at-min or stuck-at-max
  for (int ch = 0; ch < SENSOR_COUNT; ch++) {
    selectChannel(ch);
    delayMicroseconds(MUX_SETTLE_US);
    int v = analogRead(MUX_SIG);
    if (v < 10 || v > (ADC_MAX - 10)) {
      sensorOK = false;
      sensorFail++;
    }
  }

  // ── EEPROM: check magic number
  uint16_t magic;
  EEPROM.get(EEPROM_MAGIC_ADDR, magic);
  eepromOK = (magic == EEPROM_VERSION);

  // ── Calibration: verify Min < Ref < Max for all sensors
  for (int i = 0; i < SENSOR_COUNT && calibOK; i++) {
    if (!(Min_ADC[i] < Reference_ADC[i] && Reference_ADC[i] < Max_ADC[i])) {
      calibOK = false;
    }
  }

  // ── Motors: brief low-PWM pulse (50ms) — just enough to verify driver responds
  motor(40, 40);  delay(50);
  motor(0, 0);

  // ── Battery
  updateBattery(true);
  battOK = (batteryMV > cfg.batt_cutoff_mv);

  // ── Display results on OLED
  u8g.firstPage();
  do {
    u8g.setFont(u8g_font_7x14B);
    u8g.drawStr(10, 12, "SYSTEM CHECK");
    u8g.setFont(u8g_font_profont12);

    // Sensor line
    u8g.drawStr(2, 25, sensorOK ? "* Sensors (14/14)" : "! Sensors FAULT");
    if (!sensorOK) {
      u8g.setPrintPos(90, 25); u8g.print(sensorFail); u8g.print(F(" bad"));
    }

    // EEPROM
    u8g.drawStr(2, 36, eepromOK ? "* EEPROM OK" : "! EEPROM EMPTY");

    // Calibration
    u8g.drawStr(2, 47, calibOK ? "* Calibration OK" : "! NOT Calibrated");

    // Battery voltage
    u8g.setPrintPos(2, 58);
    u8g.print(battOK ? "* Batt " : "! LOW  ");
    u8g.print(batteryMV / 1000.0f, 2);
    u8g.print(F("V"));

    // CPU speed
    u8g.setPrintPos(80, 58);
    u8g.print(SystemCoreClock / 1000000);
    u8g.print(F("MHz"));
  } while (u8g.nextPage());

  // Hold self-test screen 2 seconds (or until SELECT pressed)
  uint32_t t = millis();
  while (millis() - t < 2000) {
    if (digitalRead(BTN_SELECT) == LOW) break;
  }

  // If calibration is missing, auto-navigate to calibration reminder
  if (!calibOK) {
    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_7x14B);
      u8g.drawStr(5, 25, "Run Calibration");
      u8g.drawStr(5, 42, "before driving!");
      u8g.setFont(u8g_font_profont12);
      u8g.drawStr(20, 58, "[SELECT] to dismiss");
    } while (u8g.nextPage());
    while (digitalRead(BTN_SELECT) == HIGH);
    delay(300);
  }
}

// ═════════════════════════════════════════════════════════════════
// RUN STATISTICS
// ═════════════════════════════════════════════════════════════════
void startRunStats() {
  memset(&currentStats, 0, sizeof(RunStats));
  currentStats.min_battery_mv = batteryMV;
  runStartTime = millis();
}

void updateRunStats(float err, int lspd, int rspd) {
  currentStats.run_time_ms  = millis() - runStartTime;
  float absErr = fabsf(err);
  if (absErr > currentStats.max_error) currentStats.max_error = absErr;
  // Running average error (exponential)
  currentStats.avg_error = currentStats.avg_error * 0.99f + absErr * 0.01f;
  int spd = max(abs(lspd), abs(rspd));
  if (spd > currentStats.max_speed_reached) currentStats.max_speed_reached = spd;
}

void finalizeRunStats() {
  currentStats.run_time_ms = millis() - runStartTime;
  memcpy(&lastStats, &currentStats, sizeof(RunStats));
}

// Show post-run statistics on OLED — called automatically after every PID run
void showPostRunStats() {
  while (1) {
    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_7x14B);
      u8g.drawStr(15, 12, "RUN COMPLETE");
      u8g.setFont(u8g_font_profont12);

      // Time
      u8g.setPrintPos(0, 25);
      u8g.print(F("Time: "));
      u8g.print(lastStats.run_time_ms / 1000.0f, 2);
      u8g.print(F("s"));

      // Lost-line events
      u8g.setPrintPos(0, 35);
      u8g.print(F("Lost: "));
      u8g.print(lastStats.lost_line_events);
      u8g.print(F("  Cross: "));
      u8g.print(lastStats.crossing_events);

      // Error
      u8g.setPrintPos(0, 45);
      u8g.print(F("MaxErr: "));
      u8g.print(lastStats.max_error, 1);
      u8g.print(F("  Avg: "));
      u8g.print(lastStats.avg_error, 1);

      // Speed + battery
      u8g.setPrintPos(0, 55);
      u8g.print(F("MaxSpd:"));
      u8g.print(lastStats.max_speed_reached);
      u8g.print(F("  Batt:"));
      u8g.print(lastStats.min_battery_mv / 1000.0f, 1);
      u8g.print(F("V"));
    } while (u8g.nextPage());

    ButtonEvent e = readButtonBlocking(BTN_SELECT);
    if (e == BTN_SELECT_SHORT || e == BTN_BACK_SHORT) return;
    ButtonEvent e2 = readButtonBlocking(BTN_BACK);
    if (e2 == BTN_BACK_SHORT) return;
  }
}

// ═════════════════════════════════════════════════════════════════
// COMPETITION MODE — one-press optimised launch
// Disables OLED (if configured), loads selected profile, counts down
// ═════════════════════════════════════════════════════════════════
void competitionMode() {
  // Display competition setup screen
  int  sel      = 0;
  bool launched = false;

  while (!launched) {
    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_7x14B);
      u8g.drawStr(10, 12, "COMPETITION");
      u8g.setFont(u8g_font_profont12);

      u8g.setPrintPos(2, 27);
      u8g.print(F("Profile: "));
      u8g.print(cfg.profiles[cfg.active_profile].name);

      u8g.setPrintPos(2, 39);
      u8g.print(F("Delay:   "));
      u8g.print(cfg.start_delay_sec);
      u8g.print(F("s"));

      u8g.setPrintPos(2, 51);
      u8g.print(F("OLED:    "));
      const char* oledModes[] = {"ON", "DIM", "OFF"};
      u8g.print(oledModes[cfg.oled_dim_run]);

      // Highlight selected row
      u8g.drawBox(0, 17 + sel * 12, 3, 10);

      u8g.drawStr(30, 63, "[SEL]=LAUNCH");
    } while (u8g.nextPage());

    ButtonEvent e = pollButtons();
    switch (e) {
      case BTN_UP_SHORT:   sel = max(0, sel - 1);                  break;
      case BTN_DOWN_SHORT: sel = min(2, sel + 1);                  break;
      case BTN_SELECT_SHORT:
        if (sel == 0) cfg.active_profile = (cfg.active_profile + 1) % 3;
        if (sel == 1) cfg.start_delay_sec = (cfg.start_delay_sec == 0) ? 3 :
                      (cfg.start_delay_sec == 3) ? 5 :
                      (cfg.start_delay_sec == 5) ? 10 : 0;
        if (sel == 2) cfg.oled_dim_run = (cfg.oled_dim_run + 1) % 3;
        break;
      case BTN_BACK_SHORT: return;   // abort
      case BTN_SELECT_LONG: launched = true; break;   // long press = LAUNCH
      default: break;
    }
  }

  // ── Countdown
  activeP = &cfg.profiles[cfg.active_profile];
  live_speed_offset = 0;
  turbo_active      = false;

  for (int cnt = cfg.start_delay_sec; cnt > 0; cnt--) {
    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_10x20);
      char buf[4]; itoa(cnt, buf, 10);
      u8g.drawStr(54, 48, buf);
      u8g.setFont(u8g_font_profont12);
      u8g.drawStr(20, 12, "GET READY...");
    } while (u8g.nextPage());
    delay(900);
    // Brief LED flash on each count
    digitalWrite(LED_PIN, LED_ON); delay(100); digitalWrite(LED_PIN, LED_OFF);
  }

  // ── GO! Flash LED, then launch
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, LED_ON); delay(80);
    digitalWrite(LED_PIN, LED_OFF); delay(80);
  }

  runLineFollow(FOLLOW_TURBO);   // competition = always turbo mode
  showPostRunStats();
}

// ═════════════════════════════════════════════════════════════════
// SYSTEM INFO SCREEN
// ═════════════════════════════════════════════════════════════════
void showSystemInfo() {
  while (1) {
    updateBattery(false);
    u8g.firstPage();
    do {
      u8g.setFont(u8g_font_7x14B);
      u8g.drawStr(15, 12, "SYSTEM INFO");
      u8g.setFont(u8g_font_profont12);

      u8g.setPrintPos(0, 25);
      u8g.print(F("FW: v" FIRMWARE_VERSION "  " FIRMWARE_DATE));

      u8g.setPrintPos(0, 35);
      u8g.print(F("CPU: "));
      u8g.print(SystemCoreClock / 1000000);
      u8g.print(F(" MHz"));

      u8g.setPrintPos(0, 45);
      u8g.print(F("Batt: "));
      u8g.print(batteryMV / 1000.0f, 2);
      u8g.print(F("V  ADC:"));
      u8g.print(cfg.adc_bits);
      u8g.print(F("bit"));

      u8g.setPrintPos(0, 55);
      u8g.print(F("Profile: "));
      u8g.print(activeP->name);
      u8g.print(F("  Kp="));
      u8g.print(activeP->kp, 0);

      u8g.setPrintPos(0, 63);
      u8g.print(F("EEPROM: ~212/1024B used"));
    } while (u8g.nextPage());

    if (pollButtons() == BTN_BACK_SHORT) return;
  }
}
