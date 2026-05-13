#include "config.h"

#define MENU_ROOT         0
#define SUB_LINEFOLLOW    1
#define SUB_CALIBRATE     2
#define SUB_PIDTUNE       3
#define SUB_DRIVEMODES    4
#define SUB_SENSORTEST    5
#define SUB_MOTORTEST     6
#define SUB_SETTINGS      7
#define SUB_COMPMODE      8
#define SUB_PROFILES      9
#define SUB_SYSINFO      10

static uint8_t menuPage = MENU_ROOT;
static uint8_t menuCursor = 0;
static bool menuEditing = false;

static uint32_t lastButtonTime = 0;
static uint8_t lastButton = 255;

void pollButtons() {
  if (millis() - lastButtonTime < 30) return;
  uint8_t nowBtn = 255;
  if (digitalRead(BTN_UP)==LOW) nowBtn=BTN_UP;
  else if (digitalRead(BTN_DOWN)==LOW) nowBtn=BTN_DOWN;
  else if (digitalRead(BTN_SELECT)==LOW) nowBtn=BTN_SELECT;
  else if (digitalRead(BTN_BACK)==LOW) nowBtn=BTN_BACK;
  if (nowBtn != lastButton) {
    lastButton = nowBtn;
    lastButtonTime = millis();
  }
}

bool isButtonPressed(uint8_t btn) {
  return (lastButton == btn && millis()-lastButtonTime < 500);
}

void waitForButtonRelease() {
  while (digitalRead(BTN_UP)==LOW || digitalRead(BTN_DOWN)==LOW ||
         digitalRead(BTN_SELECT)==LOW || digitalRead(BTN_BACK)==LOW) delay(10);
  lastButton = 255;
}

void menuInit() {
  menuState = 1;
  menuPage = MENU_ROOT;
  menuCursor = 0;
  menuEditing = false;
}

// Universal value editor for integers with decimal display
void menuEditValue(const char* name, int32_t &value, int32_t step, int32_t minV, int32_t maxV, uint8_t decimals) {
  bool editing = true;
  while (editing) {
    pollButtons();
    u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf);
    char buf[32];
    if (decimals == 0) {
      sprintf(buf, "%s: %ld", name, (long)value);
    } else if (decimals == 2) {
      sprintf(buf, "%s: %ld.%02ld", name, (long)(value/100), (long)(abs(value)%100));
    } else if (decimals == 3) {
      sprintf(buf, "%s: %ld.%03ld", name, (long)(value/1000), (long)(abs(value)%1000));
    } else {
      sprintf(buf, "%s: %ld", name, (long)value);
    }
    u8g2.drawStr(5,30, buf);
    u8g2.drawStr(5,50, "UP/DN adjust, SEL ok");
    u8g2.sendBuffer();
    if (isButtonPressed(BTN_UP)) { value+=step; if(value>maxV)value=maxV; waitForButtonRelease(); }
    if (isButtonPressed(BTN_DOWN)) { value-=step; if(value<minV)value=minV; waitForButtonRelease(); }
    if (isButtonPressed(BTN_SELECT)) { editing=false; waitForButtonRelease(); }
  }
}

// ================== ROOT ==================
static const char* rootItems[] = {
  "LF","Calib","PID","Drv","Sens","Mot","Set","Comp","Prof","Info"
};
void drawRoot() { drawMenu("ROOT", rootItems, 10, menuCursor, false); }
void handleRoot() {
  if (isButtonPressed(BTN_UP) && menuCursor>0) { menuCursor--; waitForButtonRelease(); }
  if (isButtonPressed(BTN_DOWN) && menuCursor<9) { menuCursor++; waitForButtonRelease(); }
  if (isButtonPressed(BTN_SELECT)) {
    waitForButtonRelease();
    if (menuCursor==0) { menuPage=SUB_LINEFOLLOW; menuCursor=0; }
    else if (menuCursor==1) { menuPage=SUB_CALIBRATE; menuCursor=0; }
    else if (menuCursor==2) { menuPage=SUB_PIDTUNE; menuCursor=0; }
    else if (menuCursor==3) { menuPage=SUB_DRIVEMODES; menuCursor=0; }
    else if (menuCursor==4) { menuPage=SUB_SENSORTEST; menuCursor=0; }
    else if (menuCursor==5) { menuPage=SUB_MOTORTEST; menuCursor=0; }
    else if (menuCursor==6) { menuPage=SUB_SETTINGS; menuCursor=0; }
    else if (menuCursor==7) { menuPage=SUB_COMPMODE; menuCursor=0; }
    else if (menuCursor==8) { menuPage=SUB_PROFILES; menuCursor=0; }
    else if (menuCursor==9) { menuPage=SUB_SYSINFO; menuCursor=0; }
  }
  if (isButtonPressed(BTN_BACK)) { waitForButtonRelease(); menuState=0; drawFrontPage(); }
}

// ===== Line Follow =====
static const char* lfItems[] = {"Std","Snap","LEdge","REdge","Turbo","Back"};
void drawLineFollow() { drawMenu("LineFollow", lfItems, 6, menuCursor, false); }
void handleLineFollow() {
  if (isButtonPressed(BTN_UP) && menuCursor>0) { menuCursor--; waitForButtonRelease(); }
  if (isButtonPressed(BTN_DOWN) && menuCursor<5) { menuCursor++; waitForButtonRelease(); }
  if (isButtonPressed(BTN_SELECT)) {
    waitForButtonRelease();
    if (menuCursor==5) { menuPage=MENU_ROOT; menuCursor=0; return; }
    menuState=2; runPID(menuCursor);
  }
  if (isButtonPressed(BTN_BACK)) { waitForButtonRelease(); menuPage=MENU_ROOT; menuCursor=0; }
}

// ===== Calibration =====
static const char* calItems[] = {"Auto","WhtSnap","BlkSnap","View","Verify","Reset","Back"};
void drawCalibrate() { drawMenu("Calibrate", calItems, 7, menuCursor, false); }
void handleCalibrate() {
  if (isButtonPressed(BTN_UP) && menuCursor>0) { menuCursor--; waitForButtonRelease(); }
  if (isButtonPressed(BTN_DOWN) && menuCursor<6) { menuCursor++; waitForButtonRelease(); }
  if (isButtonPressed(BTN_SELECT)) {
    waitForButtonRelease();
    switch (menuCursor) {
      case 0: autoCalibrate(); break;
      case 1: manualCalibrateWhite(); break;
      case 2: manualCalibrateBlack(); cal.calibrated=true; EEPROM.put(EEPROM_CAL_START,cal); EEPROM.write(EEPROM_CAL_START,EEPROM_MARKER); break;
      case 3: break; // view (omitted for size)
      case 4: verifyCalibration(); break;
      case 5: cal.calibrated=false; EEPROM.write(EEPROM_CAL_START,0); break;
      case 6: menuPage=MENU_ROOT; menuCursor=0; break;
    }
  }
  if (isButtonPressed(BTN_BACK)) { waitForButtonRelease(); menuPage=MENU_ROOT; menuCursor=0; }
}

// ===== PID Tune =====
static const char* pidTuneItems[] = {"Kp","Ki","Kd","BaseSpd","MaxSpd","Turbo","DerAlpha","ILim","AccRamp","Save","Load","Back"};
void drawPIDTune() { drawMenu("PID Tune", pidTuneItems, 12, menuCursor, false); }
void handlePIDTune() {
  if (isButtonPressed(BTN_UP) && menuCursor>0) { menuCursor--; waitForButtonRelease(); }
  if (isButtonPressed(BTN_DOWN) && menuCursor<11) { menuCursor++; waitForButtonRelease(); }
  if (isButtonPressed(BTN_SELECT)) {
    waitForButtonRelease();
    switch (menuCursor) {
      case 0: menuEditValue("Kp", (int32_t&)activePID->kp, 50, 0, 5000, 3); break;
      case 1: menuEditValue("Ki", (int32_t&)activePID->ki, 10, 0, 1000, 3); break;
      case 2: menuEditValue("Kd", (int32_t&)activePID->kd, 100, 0, 10000, 3); break;
      case 3: menuEditValue("BaseSpd", (int32_t&)settings.baseSpeed, 5, 0, 255, 0); break;
      case 4: menuEditValue("MaxSpd", (int32_t&)settings.maxSpeed, 5, 0, 255, 0); break;
      case 5: menuEditValue("Turbo%", (int32_t&)settings.turboMultiplier, 10, 100, 300, 2); break; // scaled*100
      case 6: menuEditValue("DerAlpha", (int32_t&)activePID->derivativeAlpha, 50, 50, 1000, 3); break;
      case 7: menuEditValue("ILim", (int32_t&)activePID->integralLimit, 5, 0, 300, 0); break;
      case 8: menuEditValue("AccRamp", (int32_t&)settings.accelRampMs, 50, 0, 1000, 0); break;
      case 9: saveSettings(); drawSimpleMessage("Saved"); delay(500); break;
      case 10: loadSettings(); drawSimpleMessage("Loaded"); delay(500); break;
      case 11: menuPage=MENU_ROOT; menuCursor=0; break;
    }
  }
  if (isButtonPressed(BTN_BACK)) { waitForButtonRelease(); menuPage=MENU_ROOT; menuCursor=0; }
}

// ===== Drive Modes =====
static const char* driveItems[] = {"LEdgeOfs","REdgeOfs","LL Mode","BridgeMs","SpinTO","CrossMod","CrossMs","Back"};
void drawDriveModes() { drawMenu("DriveMode", driveItems, 8, menuCursor, false); }
void handleDriveModes() {
  if (isButtonPressed(BTN_UP) && menuCursor>0) { menuCursor--; waitForButtonRelease(); }
  if (isButtonPressed(BTN_DOWN) && menuCursor<7) { menuCursor++; waitForButtonRelease(); }
  if (isButtonPressed(BTN_SELECT)) {
    waitForButtonRelease();
    switch (menuCursor) {
      case 0: menuEditValue("LOfs", (int32_t&)settings.edgeOffsetLeft, 1, 0, NUM_SENSORS-1, 0); break;
      case 1: menuEditValue("ROfs", (int32_t&)settings.edgeOffsetRight, 1, 0, NUM_SENSORS-1, 0); break;
      case 2: { settings.lostLineMode = (settings.lostLineMode+1)%3;
                drawSimpleMessage(settings.lostLineMode==0?"Bridge":settings.lostLineMode==1?"Spin":"Stop"); delay(800); } break;
      case 3: menuEditValue("BridgeMs", (int32_t&)settings.bridgeHoldMs, 50, 0, 2000, 0); break;
      case 4: menuEditValue("SpinTO", (int32_t&)settings.spinTimeoutMs, 100, 500, 5000, 0); break;
      case 5: { settings.crossingMode = (settings.crossingMode+1)%4;
                drawSimpleMessage(settings.crossingMode==0?"Str":settings.crossingMode==1?"Seq":settings.crossingMode==2?"Left":"Right"); delay(800); } break;
      case 6: menuEditValue("CrossMs", (int32_t&)settings.crossingTransitMs, 50, 0, 1000, 0); break;
      case 7: menuPage=MENU_ROOT; menuCursor=0; break;
    }
  }
  if (isButtonPressed(BTN_BACK)) { waitForButtonRelease(); menuPage=MENU_ROOT; menuCursor=0; }
}

// ===== Sensor Test =====
static const char* sensItems[] = {"Analog","Digital","Pos","Noise","Health","Back"};
void drawSensorTest() { drawMenu("SensTest", sensItems, 6, menuCursor, false); }
void handleSensorTest() {
  if (isButtonPressed(BTN_UP) && menuCursor>0) { menuCursor--; waitForButtonRelease(); }
  if (isButtonPressed(BTN_DOWN) && menuCursor<5) { menuCursor++; waitForButtonRelease(); }
  if (isButtonPressed(BTN_SELECT)) {
    waitForButtonRelease();
    if (menuCursor==5) { menuPage=MENU_ROOT; menuCursor=0; return; }
    if (menuCursor==0) {
      while (!isButtonPressed(BTN_BACK)) {
        pollButtons();
        uint16_t raw[NUM_SENSORS];
        for (uint8_t i=0;i<NUM_SENSORS;i++) raw[i]=readSensor(i);
        drawSensorBars(raw, 500);
        delay(50);
      }
      waitForButtonRelease();
    } else if (menuCursor==4) {
      bool ok = checkSensorHealth();
      drawSimpleMessage(ok?"OK":"FAIL");
      delay(1500);
    }
  }
  if (isButtonPressed(BTN_BACK)) { waitForButtonRelease(); menuPage=MENU_ROOT; menuCursor=0; }
}

// ===== Motor Test =====
static const char* motItems[] = {"Fwd","Rev","LOnly","ROnly","SL","SR","Sweep","Dead","Back"};
void drawMotorTest() { drawMenu("MotTest", motItems, 9, menuCursor, false); }
void handleMotorTest() {
  if (isButtonPressed(BTN_UP) && menuCursor>0) { menuCursor--; waitForButtonRelease(); }
  if (isButtonPressed(BTN_DOWN) && menuCursor<8) { menuCursor++; waitForButtonRelease(); }
  if (isButtonPressed(BTN_SELECT)) {
    waitForButtonRelease();
    if (menuCursor==8) { menuPage=MENU_ROOT; menuCursor=0; return; }
    switch (menuCursor) {
      case 0: setMotors(200,200); delay(500); motorBrake(); break;
      case 1: setMotors(-200,-200); delay(500); motorBrake(); break;
      case 2: setMotors(200,0); delay(500); motorBrake(); break;
      case 3: setMotors(0,200); delay(500); motorBrake(); break;
      case 4: setMotors(-200,200); delay(500); motorBrake(); break;
      case 5: setMotors(200,-200); delay(500); motorBrake(); break;
      case 6: for(int p=0;p<=255;p+=5){setMotors(p,p);delay(20);} for(int p=255;p>=0;p-=5){setMotors(p,p);delay(20);} motorBrake(); break;
      case 7: menuEditValue("DB L", (int32_t&)settings.deadBandLeft, 1, 0, 100, 0);
               menuEditValue("DB R", (int32_t&)settings.deadBandRight, 1, 0, 100, 0); break;
    }
  }
  if (isButtonPressed(BTN_BACK)) { waitForButtonRelease(); menuPage=MENU_ROOT; menuCursor=0; }
}

// ===== Settings =====
static const char* setItems[] = {"CPU","Line","ADC","Baud","Telem","OLED","StartD","BatCut","Reset","Back"};
void drawSettings() { drawMenu("Settings", setItems, 10, menuCursor, false); }
void handleSettings() {
  if (isButtonPressed(BTN_UP) && menuCursor>0) { menuCursor--; waitForButtonRelease(); }
  if (isButtonPressed(BTN_DOWN) && menuCursor<9) { menuCursor++; waitForButtonRelease(); }
  if (isButtonPressed(BTN_SELECT)) {
    waitForButtonRelease();
    if (menuCursor==9) { menuPage=MENU_ROOT; menuCursor=0; return; }
    switch (menuCursor) {
      case 0: { const uint8_t sp[]={72,96,128}; uint8_t idx=(settings.cpuSpeedMHz==72?0:(settings.cpuSpeedMHz==96?1:2)+1)%3;
                settings.cpuSpeedMHz=sp[idx]; drawSimpleMessage("CPU",idx==0?"72":idx==1?"96":"128"); delay(800); } break;
      case 1: settings.lineMode=1-settings.lineMode; drawSimpleMessage(settings.lineMode?"Light":"Dark"); delay(800); break;
      case 2: { const uint8_t res[]={8,10,12}; uint8_t i=(settings.adcResolution==8?0:(settings.adcResolution==10?1:2)+1)%3;
                settings.adcResolution=res[i]; drawSimpleMessage("ADC",i==0?"8":i==1?"10":"12"); delay(800); } break;
      case 3: { uint32_t bd[]={9600,19200,38400,57600,115200,230400}; uint8_t bi=0;
                while(bi<6&&bd[bi]!=settings.serialBaud)bi++; bi=(bi+1)%6;
                settings.serialBaud=bd[bi]; char tmp[10]; sprintf(tmp,"%lu",bd[bi]); drawSimpleMessage("Baud",tmp); delay(800); } break;
      case 4: settings.telemetryMode=(settings.telemetryMode+1)%3; drawSimpleMessage("Telem",settings.telemetryMode==0?"Off":settings.telemetryMode==1?"Err":"Full"); delay(800); break;
      case 5: settings.oledDuringRun=(settings.oledDuringRun+1)%3; drawSimpleMessage("OLED",settings.oledDuringRun==0?"On":settings.oledDuringRun==1?"Dim":"Off"); delay(800); break;
      case 6: { const uint8_t dly[]={0,3,5,10}; uint8_t di=(settings.startDelay==0?0:settings.startDelay==3?1:settings.startDelay==5?2:3+1)%4;
                settings.startDelay=dly[di]; char tmp[8]; sprintf(tmp,"%us",dly[di]); drawSimpleMessage("Delay",tmp); delay(800); } break;
      case 7: menuEditValue("BatCut mV", (int32_t&)settings.batteryCutoff, 100, 4000, 15000, 0); break;
      case 8: resetFactory(); saveSettings(); drawSimpleMessage("F.Reset"); delay(1000); break;
    }
  }
  if (isButtonPressed(BTN_BACK)) { waitForButtonRelease(); menuPage=MENU_ROOT; menuCursor=0; }
}

// ===== Comp Mode =====
static const char* compItems[] = {"ProfA","ProfB","ProfC","Delay","GO","Back"};
void drawCompMode() { drawMenu("Comp", compItems, 6, menuCursor, false); }
void handleCompMode() {
  if (isButtonPressed(BTN_UP) && menuCursor>0) { menuCursor--; waitForButtonRelease(); }
  if (isButtonPressed(BTN_DOWN) && menuCursor<5) { menuCursor++; waitForButtonRelease(); }
  if (isButtonPressed(BTN_SELECT)) {
    waitForButtonRelease();
    if (menuCursor==5) { menuPage=MENU_ROOT; menuCursor=0; return; }
    if (menuCursor<3) { settings.activeProfile=menuCursor; saveSettings();
      activePID = (menuCursor==0?&settings.pidSafe:(menuCursor==1?&settings.pidNormal:&settings.pidTurbo));
      drawSimpleMessage("Profile set"); delay(500);
    } else if (menuCursor==3) {
      menuEditValue("StartDelay", (int32_t&)settings.startDelay, 1, 0, 10, 0);
    } else { startCompetition(settings.activeProfile); }
  }
  if (isButtonPressed(BTN_BACK)) { waitForButtonRelease(); menuPage=MENU_ROOT; menuCursor=0; }
}

// ===== Profiles =====
static const char* profItems[] = {"EdSafe","EdNorm","EdTurbo","CpyA>B","CpyB>C","CpyC>A","Back"};
void drawProfiles() { drawMenu("Profiles", profItems, 7, menuCursor, false); }
void handleProfiles() {
  if (isButtonPressed(BTN_UP) && menuCursor>0) { menuCursor--; waitForButtonRelease(); }
  if (isButtonPressed(BTN_DOWN) && menuCursor<6) { menuCursor++; waitForButtonRelease(); }
  if (isButtonPressed(BTN_SELECT)) {
    waitForButtonRelease();
    if (menuCursor==6) { menuPage=MENU_ROOT; menuCursor=0; return; }
    if (menuCursor<3) {
      activePID = (menuCursor==0?&settings.pidSafe:(menuCursor==1?&settings.pidNormal:&settings.pidTurbo));
      menuPage=SUB_PIDTUNE; menuCursor=0;
    } else if (menuCursor==3) { settings.pidNormal=settings.pidSafe; saveSettings(); }
    else if (menuCursor==4) { settings.pidTurbo=settings.pidNormal; saveSettings(); }
    else { settings.pidSafe=settings.pidTurbo; saveSettings(); }
  }
  if (isButtonPressed(BTN_BACK)) { waitForButtonRelease(); menuPage=MENU_ROOT; menuCursor=0; }
}

// ===== System Info =====
void drawSysInfo() {
  u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf);
  char buf[32];
  sprintf(buf,"FW: v2.0");
  u8g2.drawStr(0,10,buf);
  sprintf(buf,"CPU:%dMHz",settings.cpuSpeedMHz);
  u8g2.drawStr(0,25,buf);
  sprintf(buf,"Bat:%umV",readBattery_mV());
  u8g2.drawStr(0,40,buf);
  sprintf(buf,"EEPROM used");
  u8g2.drawStr(0,55,buf);
  u8g2.sendBuffer();
}
void handleSysInfo() {
  if (isButtonPressed(BTN_BACK)) { waitForButtonRelease(); menuPage=MENU_ROOT; menuCursor=0; }
}

// Main dispatcher
void menuUpdate() {
  if (menuPage==MENU_ROOT) { drawRoot(); handleRoot(); }
  else if (menuPage==SUB_LINEFOLLOW) { drawLineFollow(); handleLineFollow(); }
  else if (menuPage==SUB_CALIBRATE) { drawCalibrate(); handleCalibrate(); }
  else if (menuPage==SUB_PIDTUNE) { drawPIDTune(); handlePIDTune(); }
  else if (menuPage==SUB_DRIVEMODES) { drawDriveModes(); handleDriveModes(); }
  else if (menuPage==SUB_SENSORTEST) { drawSensorTest(); handleSensorTest(); }
  else if (menuPage==SUB_MOTORTEST) { drawMotorTest(); handleMotorTest(); }
  else if (menuPage==SUB_SETTINGS) { drawSettings(); handleSettings(); }
  else if (menuPage==SUB_COMPMODE) { drawCompMode(); handleCompMode(); }
  else if (menuPage==SUB_PROFILES) { drawProfiles(); handleProfiles(); }
  else if (menuPage==SUB_SYSINFO) { drawSysInfo(); handleSysInfo(); }
}