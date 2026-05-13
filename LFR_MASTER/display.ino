#include "config.h"

void drawSplashScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_10x20_tf);
  u8g2.drawStr(10,30,"LFR MASTER");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(20,50,"14-Sensor STM32");
  u8g2.sendBuffer();
  delay(1000);
}

void drawFrontPage() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_8x13_tf);
  u8g2.drawStr(20,20,"LINE FOLLOWER");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(30,45,"Press SELECT");
  drawBatteryIcon(readBattery_mV());
  u8g2.sendBuffer();
}

void drawMenu(const char* title, const char** items, uint8_t count, uint8_t cursor, bool editing) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0,10,title);
  for (uint8_t i=0; i<count; i++) {
    if (i==cursor) {
      u8g2.drawStr(0,25+i*10,">");
      u8g2.drawStr(12,25+i*10,items[i]);
    } else {
      u8g2.drawStr(5,25+i*10,items[i]);
    }
  }
  if (editing) u8g2.drawStr(100,10,"EDIT");
  u8g2.sendBuffer();
}

void drawRunningScreen(int32_t error, int16_t l, int16_t r, uint16_t spd, uint16_t bat) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  char buf[32];
  sprintf(buf,"E:%4ld L:%3d R:%3d", (long)error, l, r);
  u8g2.drawStr(0,10,buf);
  sprintf(buf,"Spd:%3d Bat:%u", spd, bat);
  u8g2.drawStr(0,25,buf);
  u8g2.sendBuffer();
}

void drawPostRunStats(uint32_t t, uint16_t spd) {
  drawSimpleMessage("Run Complete","Press SELECT");
}

void drawBatteryIcon(uint16_t mV) {
  char buf[8];
  sprintf(buf,"%u.%02uV", mV/1000, (mV%1000)/10);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(80,60,buf);
}

void drawSensorBars(uint16_t raw[NUM_SENSORS], uint8_t threshold) {
  u8g2.clearBuffer();
  for (uint8_t i=0; i<NUM_SENSORS; i++) {
    uint8_t h = map(raw[i],0,4095,0,63);
    u8g2.drawBox(i*9,63-h,7,h);
  }
  u8g2.sendBuffer();
}

void drawLowBatteryWarning() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_8x13_tf);
  u8g2.drawStr(10,30,"LOW BATTERY");
  u8g2.sendBuffer();
  delay(2000);
}

void drawCalibrationProgress(uint8_t step) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(20,30,"Calibrating...");
  u8g2.drawStr(20,45,step==0?"WHITE":"BLACK");
  u8g2.sendBuffer();
}

void drawSimpleMessage(const char* line1, const char* line2) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_8x13_tf);
  u8g2.drawStr(20,25,line1);
  if (line2) {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(20,45,line2);
  }
  u8g2.sendBuffer();
}

void drawProgressBar(uint8_t pct) {
  u8g2.drawFrame(20,50,88,10);
  u8g2.drawBox(22,52,84*pct/100,6);
}