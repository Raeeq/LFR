// ╔══════════════════════════════════════════════════════════════════╗
// ║  turns.ino  —  Turn Execution & Junction Management             ║
// ║                                                                 ║
// ║  Sensor-feedback turns (not timed guesses) — the robot spins   ║
// ║  until the target sensor sees the line, or until timeout fires. ║
// ║                                                                 ║
// ║  All turn functions integrate with the crossing state machine   ║
// ║  in pid.ino via executeJunction().                              ║
// ╚══════════════════════════════════════════════════════════════════╝

// ─── TURN CONFIGURATION ──────────────────────────────────────────
// Speed fraction of base_speed used during turns.
// Lower = more precise but slower. Higher = faster but may overshoot.
// ← Tune this if the robot consistently over/under-rotates at corners.
#define TURN_SPEED_FRACTION   0.55f

// Which sensors must activate to confirm turn is complete.
// For turnRight: we wait for a CENTER-RIGHT sensor (C8 = index 8).
// For turnLeft:  we wait for a CENTER-LEFT  sensor (C5 = index 5).
// ← Adjust indices if your robot geometry means a different sensor
//   re-acquires the line first after a turn.
#define TURN_CONFIRM_RIGHT_CH   8   // C8 — first center-right sensor
#define TURN_CONFIRM_LEFT_CH    5   // C5 — first center-left sensor
#define TURN_CONFIRM_CENTER_CH  6   // C6/C7 used for U-turn completion

// Minimum sensors that must activate before we accept "line found"
// during turn completion. 1 = very sensitive, 3 = more confident.
#define TURN_CONFIRM_MIN_SENSORS  1

// Safety timeout for any turn — if the sensor never fires, stop.
// ← Increase if your track has long sweeping turns that take time.
#define TURN_TIMEOUT_MS   2500

// ─── U-TURN CONFIGURATION ────────────────────────────────────────
// After spinning 180°, we look for line on the center sensors.
// A short blind spin (below) ensures we rotate past the original line
// before we start looking for the return line.
#define UTURN_BLIND_MS   400   // ms of guaranteed spin before looking

// ─── DISTANCE CALIBRATION ────────────────────────────────────────
// Set these via Motor Test to match your robot's actual movement.
// test_time_ms / measured_cm gives ms per cm at base_speed.
// ← Run driveDistanceCal() from menu to measure and save.
static uint16_t dist_cal_time_ms = 500;   // ms to travel dist_cal_cm
static uint16_t dist_cal_cm      = 30;    // cm travelled in that time

// ═════════════════════════════════════════════════════════════════
// CORE TURN PRIMITIVES
// All turns are sensor-feedback: the robot spins until the target
// sensor detects the line, then stops. Timeout prevents infinite spin.
// ═════════════════════════════════════════════════════════════════

// ── TURN RIGHT ───────────────────────────────────────────────────
// Spins clockwise until TURN_CONFIRM_RIGHT_CH sees the line.
// Called by executeJunction() — not directly from PID loop.
void turnRight() {
  int spd = (int)(activeP->base_speed * TURN_SPEED_FRACTION);
  uint32_t startTime = millis();

  // Phase 1: blind spin — move past the intersection markings first
  motor(spd, -spd);
  delay(120);

  // Phase 2: sensor-feedback spin — wait for line re-acquisition
  while (1) {
    if (millis() - startTime > TURN_TIMEOUT_MS) {
      // Safety: line never found — stop cleanly
      motor(0, 0);
      return;
    }
    readSensors();

    // Success condition: target channel (or nearby) sees the line
    if (sensorDigital[TURN_CONFIRM_RIGHT_CH] == 1 &&
        sumOnSensor >= TURN_CONFIRM_MIN_SENSORS) {
      break;
    }
    motor(spd, -spd);
  }

  motor(0, 0);
  delay(30);   // brief settle before handing back to PID
}

// ── TURN LEFT ────────────────────────────────────────────────────
// Spins counter-clockwise until TURN_CONFIRM_LEFT_CH sees the line.
void turnLeft() {
  int spd = (int)(activeP->base_speed * TURN_SPEED_FRACTION);
  uint32_t startTime = millis();

  // Phase 1: blind spin past intersection
  motor(-spd, spd);
  delay(120);

  // Phase 2: sensor-feedback
  while (1) {
    if (millis() - startTime > TURN_TIMEOUT_MS) {
      motor(0, 0);
      return;
    }
    readSensors();

    if (sensorDigital[TURN_CONFIRM_LEFT_CH] == 1 &&
        sumOnSensor >= TURN_CONFIRM_MIN_SENSORS) {
      break;
    }
    motor(-spd, spd);
  }

  motor(0, 0);
  delay(30);
}

// ── U-TURN (180°) ────────────────────────────────────────────────
// Spins right until the CENTER sensors re-acquire the line.
// Direction can be flipped if your track demands a left U-turn.
// ← Change motor(-spd, spd) to motor(spd, -spd) for right U-turn.
void uTurn() {
  int spd = (int)(activeP->base_speed * TURN_SPEED_FRACTION);
  uint32_t startTime = millis();

  // Phase 1: blind spin — guaranteed rotation past original heading
  motor(-spd, spd);
  delay(UTURN_BLIND_MS);

  // Phase 2: sensor-feedback — wait for center sensor activation
  while (1) {
    if (millis() - startTime > TURN_TIMEOUT_MS * 2) {
      motor(0, 0);
      return;
    }
    readSensors();

    // Accept center sensors (C6 or C7) for completion
    if ((sensorDigital[TURN_CONFIRM_CENTER_CH] ||
         sensorDigital[TURN_CONFIRM_CENTER_CH + 1]) &&
        sumOnSensor >= TURN_CONFIRM_MIN_SENSORS) {
      break;
    }
    motor(-spd, spd);
  }

  motor(0, 0);
  delay(30);
}

// ── SHARP TURN (in-place pivot, no sensor feedback) ──────────────
// Used for very sharp corners where sensor feedback is unreliable
// because the line is immediately beside the robot.
// duration_ms: how long to pivot (calibrate for your wheelbase).
// ← Tune duration_ms to match a 90° turn on your specific robot.
void turnSharpRight(uint16_t duration_ms) {
  int spd = (int)(activeP->base_speed * 0.7f);
  motor(spd, -spd);
  delay(duration_ms);
  motor(0, 0);
}

void turnSharpLeft(uint16_t duration_ms) {
  int spd = (int)(activeP->base_speed * 0.7f);
  motor(-spd, spd);
  delay(duration_ms);
  motor(0, 0);
}

// ═════════════════════════════════════════════════════════════════
// JUNCTION TYPE DETECTION
// Analyses the current bitSensor pattern to classify what kind of
// junction/feature the robot is sitting on.
// Call this WHILE on the wide-black zone (sumOnSensor ≥ threshold).
// ═════════════════════════════════════════════════════════════════
typedef enum : uint8_t {
  JCT_UNKNOWN    = 0,
  JCT_T_LEFT     = 1,   // T-junction — path continues left + straight
  JCT_T_RIGHT    = 2,   // T-junction — path continues right + straight
  JCT_T_BOTH     = 3,   // T-junction — all three directions
  JCT_CROSS      = 4,   // full crossroads
  JCT_DEAD_END   = 5,   // no path forward — must U-turn
  JCT_WIDE_BLOK  = 6,   // solid wide black block (F-ONE box etc.)
  JCT_LINE_END   = 7    // line terminates (all sensors lit equally)
} JunctionType;

JunctionType detectJunctionType() {
  // Use outer sensors to determine which directions have a continuing path.
  // Left outer  = C0–C2, Right outer = C11–C13, Center = C5–C8.
  bool hasLeft    = (sensorDigital[0] || sensorDigital[1] || sensorDigital[2]);
  bool hasRight   = (sensorDigital[11]|| sensorDigital[12]|| sensorDigital[13]);
  bool hasCenter  = (sensorDigital[5] || sensorDigital[6] ||
                     sensorDigital[7] || sensorDigital[8]);
  bool allActive  = (sumOnSensor >= SENSOR_COUNT - 2);  // nearly all lit

  if (allActive)    return JCT_WIDE_BLOK;  // solid block — just drive through
  if (hasLeft && hasRight && hasCenter) return JCT_CROSS;
  if (hasLeft && hasRight)              return JCT_T_BOTH;
  if (hasLeft  && hasCenter)            return JCT_T_LEFT;
  if (hasRight && hasCenter)            return JCT_T_RIGHT;
  if (!hasLeft && !hasRight && !hasCenter) return JCT_DEAD_END;

  return JCT_UNKNOWN;
}

// ═════════════════════════════════════════════════════════════════
// EXECUTE JUNCTION ACTION
// Called from the crossing handler in pid.ino when a wide-black zone
// is confirmed. Reads cfg.crossing_action to decide what to do.
// ═════════════════════════════════════════════════════════════════
void executeJunction() {
  JunctionType jct = detectJunctionType();

  switch (cfg.crossing_action) {

    case CROSS_STRAIGHT:
      // Just drive through — PID resumes after transit time
      // This is handled entirely in pid.ino's handleCrossing()
      break;

    case CROSS_LEFT:
      // Always turn left, regardless of junction type
      if (jct == JCT_DEAD_END) uTurn();
      else                     turnLeft();
      break;

    case CROSS_RIGHT:
      // Always turn right
      if (jct == JCT_DEAD_END) uTurn();
      else                     turnRight();
      break;

    case CROSS_SEQUENCE: {
      // Follow the programmed sequence array in LFR_MASTER.ino
      // 0=straight (handled by transit), 1=left, 2=right, 3=U-turn
      uint8_t action = crossingSequence[crossSeqIdx % CROSSING_SEQ_LEN];
      crossSeqIdx++;
      if      (action == 1) turnLeft();
      else if (action == 2) turnRight();
      else if (action == 3) uTurn();
      // action==0: fall through, transit handles straight
      break;
    }
  }
}

// ═════════════════════════════════════════════════════════════════
// DISTANCE DRIVING
// Drive a calibrated distance at base_speed.
// Calibration: menuDistanceCal() sets dist_cal_time_ms and dist_cal_cm.
// ═════════════════════════════════════════════════════════════════

// Drive forward a fixed number of centimetres (open-loop, no encoder).
// Accuracy depends on consistent battery voltage and surface friction.
void driveDistance(uint16_t cm) {
  if (dist_cal_cm == 0) return;   // uncalibrated — don't move
  uint32_t driveMs = (uint32_t)cm * dist_cal_time_ms / dist_cal_cm;
  uint32_t start   = millis();
  while (millis() - start < driveMs) {
    motor(activeP->base_speed, activeP->base_speed);
  }
  motor(0, 0);
}

// Distance calibration routine — called from Motor Test menu.
// Robot drives forward, user measures actual distance, enters value.
void distanceCalibrate() {
  u8g.firstPage();
  do {
    u8g.setFont(u8g_font_7x14B);
    u8g.drawStr(5, 15, "DIST CAL");
    u8g.setFont(u8g_font_profont12);
    u8g.drawStr(5, 30, "Robot will drive");
    u8g.drawStr(5, 42, "500ms at base spd.");
    u8g.drawStr(5, 55, "[SEL]=Start");
  } while (u8g.nextPage());

  while (digitalRead(BTN_SELECT) == HIGH);
  delay(200);

  // Drive for 500ms
  uint32_t start = millis();
  while (millis() - start < 500) motor(activeP->base_speed, activeP->base_speed);
  motor(0, 0);

  // Ask user to enter measured distance
  u8g.firstPage();
  do {
    u8g.setFont(u8g_font_profont12);
    u8g.drawStr(5, 15, "Measure distance.");
    u8g.drawStr(5, 30, "Enter cm:");
    u8g.drawStr(5, 50, "[UP/DN]=adjust");
    u8g.drawStr(5, 60, "[SEL]=confirm");
  } while (u8g.nextPage());
  delay(500);

  int measured = editInt("Measured (cm)", 30, 1, 200, 1, 10);
  dist_cal_time_ms = 500;
  dist_cal_cm      = (uint16_t)measured;

  u8g.firstPage();
  do {
    u8g.setFont(u8g_font_profont12);
    u8g.setPrintPos(5, 30);
    u8g.print(F("Saved: 500ms = "));
    u8g.print(measured);
    u8g.print(F("cm"));
  } while (u8g.nextPage());
  delay(1500);
}

// ═════════════════════════════════════════════════════════════════
// DASHED LINE BRIDGE ASSIST
// For the dashed-line section specifically on your track.
// If the robot is moving straight and briefly loses all sensors,
// this drives straight for bridge_hold_ms at the last recorded speed.
// This is already handled in pid.ino handleLostLine() Phase 1,
// but this function can be called explicitly if needed for longer gaps.
// ═════════════════════════════════════════════════════════════════
void bridgeDash(uint16_t hold_ms, int speed) {
  uint32_t start = millis();
  while (millis() - start < hold_ms) {
    motor(speed, speed);
    readSensors();
    if (sumOnSensor > 0) return;   // found line again — exit early
  }
}
