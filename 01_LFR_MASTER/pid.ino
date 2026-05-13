// ╔══════════════════════════════════════════════════════════════════╗
// ║  pid.ino  —  PID Line Follow Engine                             ║
// ║  5 modes · Look-ahead · Crossing detection · Lost-line FSM     ║
// ║  Live speed tuning · Turbo toggle · Serial telemetry           ║
// ╚══════════════════════════════════════════════════════════════════╝

// ─── PID CORRECTION LIMIT ────────────────────────────────────────
// Maximum PWM correction from PID output applied to each motor.
// Clamped after deadband compensation in motor().
#define PID_CORRECTION_MAX   255

// ─── OLED UPDATE INTERVAL DURING RUN ─────────────────────────────
// Updating OLED every loop iteration is too slow (~8 ms per frame).
// Update every 150 ms — fast enough to see, slow enough not to slow PID.
#define OLED_RUN_INTERVAL_MS   150

// ─── LIVE SPEED ADJUSTMENT LIMITS ────────────────────────────────
#define LIVE_SPEED_MIN_OFFSET  -80
#define LIVE_SPEED_MAX_OFFSET   50

// ═════════════════════════════════════════════════════════════════
// ENTRY POINT — called from menu for all follow modes
// ═════════════════════════════════════════════════════════════════
void runLineFollow(FollowMode mode) {
  // ── Initialise all PID state
  error          = 0.0f;
  previous_error = 0.0f;
  filtered_deriv = 0.0f;
  integral       = 0.0f;
  isLost         = false;
  lostLineStart  = 0;
  lastGoodError  = 0.0f;
  crossState     = CROSS_NONE;
  crossSeqIdx    = 0;
  live_speed_offset = 0;
  turbo_active   = false;
  resetRamp();

  // ── Set target position based on mode
  switch (mode) {
    case FOLLOW_LEFT:
      pid_target = CENTER_POSITION - (cfg.left_edge_offset / 10.0f);
      break;
    case FOLLOW_RIGHT:
      pid_target = CENTER_POSITION + (cfg.right_edge_offset / 10.0f);
      break;
    default:
      pid_target = CENTER_POSITION;
  }

  // ── Start statistics tracking
  startRunStats();

  // ── OLED: "RUNNING" indicator
  u8g.firstPage();
  do {
    u8g.setFont(u8g_font_7x14B);
    u8g.drawStr(30, 35, "RUNNING...");
  } while (u8g.nextPage());
  delay(300);

  uint32_t lastOLED    = 0;
  uint32_t lastLoopUs  = micros();

  // ═══════════════════════════════════════════════════════════════
  // MAIN PID LOOP
  // ═══════════════════════════════════════════════════════════════
  while (1) {
    uint32_t nowUs  = micros();
    uint32_t dt_us  = nowUs - lastLoopUs;
    lastLoopUs = nowUs;

    // ── Button handling (non-blocking poll)
    ButtonEvent btn = pollButtons();
    switch (btn) {
      case BTN_SELECT_SHORT:
        // Stop and return to menu
        motor(0, 0);
        finalizeRunStats();
        showPostRunStats();
        return;

      case BTN_BACK_SHORT:
        // Toggle turbo mode
        turbo_active = !turbo_active;
        break;

      case BTN_UP_SHORT:
        // Increase live speed (+5, capped)
        live_speed_offset = constrain(
          live_speed_offset + 5, LIVE_SPEED_MIN_OFFSET, LIVE_SPEED_MAX_OFFSET);
        break;

      case BTN_DOWN_SHORT:
        // Decrease live speed (-5)
        live_speed_offset = constrain(
          live_speed_offset - 5, LIVE_SPEED_MIN_OFFSET, LIVE_SPEED_MAX_OFFSET);
        break;

      default: break;
    }

    // ── Emergency battery cutoff
    if (batteryCritical) {
      motor(0, 0);
      drawLowBatteryWarning();   // defined in display.ino
      finalizeRunStats();
      return;
    }

    // ── Periodic battery update (non-blocking)
    updateBattery(false);

    // ── Read all 14 sensors
    readSensors();

    // ── CROSSING / WIDE-BLACK DETECTION
    // Takes priority over normal PID — intersection handling first.
    if (handleCrossing()) continue;

    // ── LOST LINE HANDLING
    if (sumOnSensor == 0) {
      if (handleLostLine(dt_us)) continue;
      else {
        // Timed out — stop and exit
        motor(0, 0);
        finalizeRunStats();
        showPostRunStats();
        return;
      }
    }

    // ── Line found — reset lost state
    if (isLost) {
      isLost = false;
      integral *= 0.5f;   // partially reset integral to prevent windup on recovery
    }

    // ── COMPUTE POSITION
    line_position = (float)weightedSum / (float)sumOnSensor;
    error = pid_target - line_position;

    // ── CENTER MODE: non-linear correction (P²) for aggressive snap
    float eff_error = error;
    if (mode == FOLLOW_CENTER) {
      // Standard P term + squared term. Feels gentle near centre,
      // very aggressive when more than a few sensors off.
      eff_error = error + 0.04f * error * fabsf(error);
    }

    // ── INTEGRAL with anti-windup clamp
    integral += eff_error;
    integral  = constrain(integral, -activeP->integral_max, activeP->integral_max);

    // ── DERIVATIVE with exponential low-pass filter
    // Alpha from profile: 1.0 = raw (noisy), 0.1 = very smooth
    float raw_d    = eff_error - previous_error;
    filtered_deriv = activeP->deriv_alpha * raw_d
                   + (1.0f - activeP->deriv_alpha) * filtered_deriv;
    previous_error = eff_error;
    lastGoodError  = eff_error;

    // ── PID CORRECTION
    float correction = activeP->kp * eff_error
                     + activeP->ki * integral
                     + activeP->kd * filtered_deriv;

    // ── LOOK-AHEAD BOOST
    // When outer wing sensors activate, a sharp curve is approaching.
    // Multiply correction by the highest active outer-sensor multiplier.
    if (cfg.lookahead_en) {
      float la_mult = 1.0f;
      for (int i = 0; i < 3; i++)
        if (sensorDigital[i] && lookaheadMult[i] > la_mult) la_mult = lookaheadMult[i];
      for (int i = 11; i < 14; i++)
        if (sensorDigital[i] && lookaheadMult[i] > la_mult) la_mult = lookaheadMult[i];
      correction *= la_mult;
    }

    // ── COMPUTE TARGET MOTOR SPEEDS
    int effective_base = activeP->base_speed + live_speed_offset;
    if (turbo_active || mode == FOLLOW_TURBO) {
      effective_base = (int)(effective_base * activeP->turbo_mult);
    }
    effective_base = constrain(effective_base, 0, activeP->max_speed);

    int targetL = constrain((int)(effective_base - correction), -PID_CORRECTION_MAX, PID_CORRECTION_MAX);
    int targetR = constrain((int)(effective_base + correction), -PID_CORRECTION_MAX, PID_CORRECTION_MAX);

    // ── APPLY WITH RAMP (prevents wheel slip on LiPo power delivery)
    motorRamped(targetL, targetR, dt_us);

    // ── UPDATE STATS
    updateRunStats(eff_error, targetL, targetR);

    // ── SERIAL TELEMETRY (disabled in TELEM_OFF mode)
    sendTelemetry(eff_error, correction, targetL, targetR);

    // ── OLED UPDATE (rate-limited — OLED is the slowest part of the loop)
    if (millis() - lastOLED > OLED_RUN_INTERVAL_MS && cfg.oled_dim_run < 2) {
      drawRunningScreen(mode, eff_error, correction, effective_base);
      lastOLED = millis();
    }
  } // end PID loop
}

// ═════════════════════════════════════════════════════════════════
// CROSSING / WIDE-BLACK HANDLER
// Returns true if crossing logic consumed this loop iteration.
// ═════════════════════════════════════════════════════════════════
static bool handleCrossing() {
  if (sumOnSensor < cfg.crossing_thresh) {
    crossState = CROSS_NONE;
    return false;
  }

  // Wide-black or intersection detected
  switch (crossState) {

    case CROSS_NONE:
      // Wide-black zone / intersection entered
      currentStats.crossing_events++;

      if (cfg.crossing_action == CROSS_STRAIGHT) {
        // Drive straight through — transit timer handles it
        crossState = CROSS_TRANSIT;
        crossStart = millis();
      } else {
        // Delegate all turn logic to turns.ino
        executeJunction();
        // Brief transit after any turn to clear the zone before PID resumes
        crossState = CROSS_TRANSIT;
        crossStart = millis();
      }
      return true;

    case CROSS_TRANSIT:
      // Drive straight through the wide zone
      if (millis() - crossStart < cfg.crossing_transit_ms) {
        motor(activeP->base_speed, activeP->base_speed);
        return true;
      }
      crossState = CROSS_NONE;
      return false;

    default:
      return false;
  }
}

// ═════════════════════════════════════════════════════════════════
// LOST LINE HANDLER
// Returns true  = handled (keep looping, motors set)
// Returns false = timed out (caller should stop)
// ═════════════════════════════════════════════════════════════════
static bool handleLostLine(uint32_t dt_us) {
  if (!isLost) {
    isLost        = true;
    lostLineStart = millis();
    currentStats.lost_line_events++;
  }

  uint32_t lostFor = millis() - lostLineStart;

  // ── Phase 1: BRIDGE — hold last motor output exactly
  // Ideal for dashed lines. The robot coasts through the gap using
  // momentum, maintaining the exact PWM that was working before.
  if (lostFor < cfg.bridge_hold_ms) {
    // motorRamped() already holds the last output — nothing to change
    motorRamped((int)rampedLeft, (int)rampedRight, dt_us);
    return true;
  }

  // ── Phase 2: SPIN — rotate toward last known direction
  if (cfg.lost_mode != LOST_STOP &&
      lostFor < cfg.bridge_hold_ms + cfg.spin_timeout_ms) {
    int spinSpd = (int)(activeP->base_speed * 0.45f);
    if (lastGoodError > 0.0f)       motor(-spinSpd,  spinSpd);  // line was left
    else if (lastGoodError < 0.0f)  motor( spinSpd, -spinSpd);  // line was right
    else                            motor(0, 0);
    return true;
  }

  // ── Phase 3: GIVE UP
  motor(0, 0);
  return false;
}

// ═════════════════════════════════════════════════════════════════
// NON-BLOCKING BUTTON POLL
// Returns a ButtonEvent without blocking the PID loop.
// Uses static state to track press duration.
// ═════════════════════════════════════════════════════════════════
ButtonEvent pollButtons() {
  static uint32_t pressStart[4] = {0};
  static bool     wasDown[4]    = {false};

  const uint8_t pins[4] = {BTN_UP, BTN_DOWN, BTN_SELECT, BTN_BACK};
  const ButtonEvent shortEvt[4] = {
    BTN_UP_SHORT, BTN_DOWN_SHORT, BTN_SELECT_SHORT, BTN_BACK_SHORT
  };
  const ButtonEvent longEvt[4] = {
    BTN_UP_HOLD, BTN_DOWN_HOLD, BTN_SELECT_LONG, BTN_BACK_LONG
  };

  for (int i = 0; i < 4; i++) {
    bool down = (digitalRead(pins[i]) == LOW);
    if (down && !wasDown[i]) {
      pressStart[i] = millis();
      wasDown[i] = true;
    } else if (!down && wasDown[i]) {
      wasDown[i] = false;
      uint32_t held = millis() - pressStart[i];
      if (held > BTN_DEBOUNCE_MS) {
        return (held > 600) ? longEvt[i] : shortEvt[i];
      }
    }
  }
  return BTN_NONE;
}

// ─── Blocking version (for menu use) ─────────────────────────────
ButtonEvent readButtonBlocking(uint8_t pin) {
  // Wait for any press then return event
  while (digitalRead(pin) == HIGH);
  uint32_t start = millis();
  while (digitalRead(pin) == LOW);
  delay(BTN_DEBOUNCE_MS);
  uint32_t held = millis() - start;
  // Map to appropriate event based on which pin
  if (pin == BTN_SELECT) return (held > 600) ? BTN_SELECT_LONG  : BTN_SELECT_SHORT;
  if (pin == BTN_BACK)   return (held > 600) ? BTN_BACK_LONG    : BTN_BACK_SHORT;
  if (pin == BTN_UP)     return (held > 600) ? BTN_UP_HOLD      : BTN_UP_SHORT;
  return                        (held > 600) ? BTN_DOWN_HOLD     : BTN_DOWN_SHORT;
}

// ═════════════════════════════════════════════════════════════════
// SERIAL TELEMETRY
// ═════════════════════════════════════════════════════════════════
void sendTelemetry(float err, float corr, int lspd, int rspd) {
  if (cfg.telemetry_mode == TELEM_OFF) return;

  if (cfg.telemetry_mode == TELEM_ERROR) {
    // Compact — suitable for Serial Plotter (4 variables)
    Serial.print(err);    Serial.print(',');
    Serial.println(corr);
    return;
  }

  // TELEM_FULL — all PID variables + motor speeds
  // Open Arduino Serial Plotter to graph in real-time.
  Serial.print(F("pos:"));   Serial.print(line_position, 1); Serial.print(',');
  Serial.print(F("err:"));   Serial.print(err, 1);           Serial.print(',');
  Serial.print(F("corr:"));  Serial.print(corr, 1);          Serial.print(',');
  Serial.print(F("int:"));   Serial.print(integral, 1);      Serial.print(',');
  Serial.print(F("L:"));     Serial.print(lspd);             Serial.print(',');
  Serial.print(F("R:"));     Serial.println(rspd);
}

// ═════════════════════════════════════════════════════════════════
// ═════════════════════════════════════════════════════════════════
// NOTE: drawRunningScreen(), drawBatteryIcon(), drawLowBatteryWarning()
// are defined in display.ino — that is the single authoritative source.
// The PID loop calls drawRunningScreen() which resolves to display.ino.
// ═════════════════════════════════════════════════════════════════
