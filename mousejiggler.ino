#include <M5Unified.h>
#include <BleMouse.h>
#include <Preferences.h>
#include "esp_system.h"
#include "esp_sleep.h"

// Bluetooth HID
BleMouse bleMouse("M5 Mouse");

// Preferences
Preferences prefs;

// Settings
int jiggleInterval;
int jiggleDistance;
bool jigglerOn;
unsigned long lastJiggle = 0;

// Sleep timeout
unsigned long sleepTimeout = 0;
unsigned long lastActivity = 0;

// Orientation
int currentRotation = 1;  // 1=landscape USB left, 3=landscape USB right

// Pulse circle state
bool heartBeatActive = false;
unsigned long heartBeatTimestamp = 0;

// UI state machine
enum Screen { MAIN, MENU, SET_INTERVAL, SET_DISTANCE, SET_SLEEP, RESET_PAIR, SHUTDOWN };
Screen currentScreen = MAIN;
int menuIndex = 0;

// ===== Save/Load =====
void saveSettings() {
  prefs.begin("jiggler", false);
  prefs.putInt("interval", jiggleInterval);
  prefs.putInt("distance", jiggleDistance);
  prefs.putBool("enabled", jigglerOn);
  prefs.putULong("sleeptime", sleepTimeout);
  prefs.end();
}

void loadSettings() {
  prefs.begin("jiggler", true);
  jiggleInterval = prefs.getInt("interval", 2000);
  jiggleDistance = prefs.getInt("distance", 5);
  jigglerOn      = prefs.getBool("enabled", false);
  sleepTimeout   = prefs.getULong("sleeptime", 0);
  prefs.end();
}

// ===== Helpers =====
void printCentered(const char* text, int y, uint16_t color) {
  int16_t w = M5.Display.textWidth(text);
  int16_t x = (M5.Display.width() - w) / 2;
  M5.Display.setCursor(x, y);
  M5.Display.setTextColor(color);
  M5.Display.print(text);
}

void showSavedPopup() {
  M5.Display.fillRect(0, 60, M5.Display.width(), 40, BLACK);
  printCentered("Saved!", 80, GREEN);
  delay(1000);
}

// Draw battery icon with thresholds
void drawBatteryIcon(int x, int y, int level) {
  int w = 20, h = 10;
  int capW = 3;

  // Outline
  M5.Display.drawRect(x, y, w, h, WHITE);
  M5.Display.fillRect(x + w, y + 2, capW, h - 4, WHITE);

  // Battery color thresholds
  uint16_t fillColor;
  if (level < 25)      fillColor = RED;
  else if (level < 75) fillColor = YELLOW;
  else                 fillColor = GREEN;

  // Fill
  int fillW = (w - 2) * level / 100;
  if (fillW > 0) {
    M5.Display.fillRect(x + 1, y + 1, fillW, h - 2, fillColor);
  }
}

// ===== Status Bar =====
void drawStatusBar() {
  int bat = M5.Power.getBatteryLevel();
  int screenW = M5.Display.width();
  int zone = screenW / 4;  // divide into 4 columns

  M5.Display.fillRect(0, 0, screenW, 18, BLACK);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(1);

  // BLE (zone 0)
  const char* bleText = bleMouse.isConnected() ? "BLE" : "...";
  int bleX = (zone / 2) - (M5.Display.textWidth(bleText) / 2);
  M5.Display.setCursor(bleX, 4);
  M5.Display.setTextColor(bleMouse.isConnected() ? GREEN : YELLOW);
  M5.Display.print(bleText);

  // ON/OFF (zone 1)
  const char* statusText = jigglerOn ? "ON" : "OFF";
  uint16_t statusColor   = jigglerOn ? GREEN : RED;
  int statusX = zone + (zone / 2) - (M5.Display.textWidth(statusText) / 2);
  M5.Display.setCursor(statusX, 4);
  M5.Display.setTextColor(statusColor);
  M5.Display.print(statusText);

  // Pulse Circle (zone 2)
  int circleX = (2 * zone) + (zone / 2);
  int circleY = 9;    // vertically centered
  int radius  = 4;
  if (jigglerOn) {
    uint16_t color = heartBeatActive ? RED : DARKGREY;
    M5.Display.fillCircle(circleX, circleY, radius, color);
  } else {
    M5.Display.fillCircle(circleX, circleY, radius, DARKGREY);
  }

  // Battery % aligned right, icon 15px left
  char batStr[10];
  sprintf(batStr, "%d%%", bat);
  int batTxtW = M5.Display.textWidth(batStr);
  int batTxtX = screenW - batTxtW - 2;  // right-aligned
  M5.Display.setCursor(batTxtX, 4);
  M5.Display.setTextColor(WHITE);
  M5.Display.print(batStr);

  int batX = batTxtX - 15 - 20;  // 20px icon width + 15px gap
  drawBatteryIcon(batX, 4, bat);
}

// ===== Dashboard =====
void drawMain() {
  M5.Display.fillScreen(BLACK);
  drawStatusBar();

  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(1);

  printCentered("Mouse Jiggler", 30, CYAN);

  char intStr[20]; sprintf(intStr, "Interval: %d s", jiggleInterval / 1000);
  printCentered(intStr, 70, WHITE);

  char distStr[20]; sprintf(distStr, "Distance: %d px", jiggleDistance);
  printCentered(distStr, 90, WHITE);

  char sleepStr[25];
  if (sleepTimeout == 0) sprintf(sleepStr, "Sleep: Off");
  else sprintf(sleepStr, "Sleep: %lus", sleepTimeout / 1000);
  printCentered(sleepStr, 120, WHITE);

  printCentered(jigglerOn ? "Status: ON" : "Status: OFF",
                150, jigglerOn ? GREEN : RED);
}

// ===== Menu =====
void drawMenu() {
  const char* items[] = {"Interval", "Distance", "Sleep Timeout", "Reset Pairing", "Shut Down", "Back"};
  int total = 6;

  M5.Display.fillScreen(BLACK);
  drawStatusBar();
  M5.Display.setFont(&fonts::Font2);

  printCentered("Menu", 30, CYAN);

  for (int i = 0; i < total; i++) {
    if (i == menuIndex) {
      char buf[30]; sprintf(buf, "> %s <", items[i]);
      printCentered(buf, 60 + i * 20, GREEN);
    } else {
      printCentered(items[i], 60 + i * 20, WHITE);
    }
  }
}

// ===== Adjust Screen =====
void drawAdjust(const char* label, int value) {
  M5.Display.fillScreen(BLACK);
  drawStatusBar();
  M5.Display.setFont(&fonts::Font2);

  printCentered(label, 40, CYAN);

  char valStr[20]; sprintf(valStr, "Value: %d", value);
  printCentered(valStr, 80, WHITE);

  printCentered("B=Up  C=Down", 120, GREEN);
  printCentered("A=Save", 140, GREEN);
}

// ===== Auto Orientation =====
void updateOrientation() {
  float ax, ay, az;
  M5.Imu.getAccel(&ax, &ay, &az);

  int newRotation = currentRotation;
  if (ax > 0.5) newRotation = 1;       // USB left
  else if (ax < -0.5) newRotation = 3; // USB right

  if (newRotation != currentRotation) {
    currentRotation = newRotation;
    M5.Display.setRotation(currentRotation);
    drawMain();
  }
}

// ===== Setup =====
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setBrightness(80);
  currentRotation = 1;
  M5.Display.setRotation(currentRotation);

  esp_reset_reason_t reason = esp_reset_reason();
  M5.Display.fillScreen(BLACK);
  M5.Display.setFont(&fonts::Font2);
  printCentered("Mouse Jiggler OS v3.16", 40, CYAN);
  if (reason == ESP_RST_POWERON) {
    printCentered("Cold Boot", 80, GREEN);
  } else {
    printCentered("Woke from Sleep", 80, YELLOW);
  }
  delay(2000);

  bleMouse.begin();
  loadSettings();
  drawMain();
  lastActivity = millis();
}

// ===== Loop =====
void loop() {
  M5.update();
  updateOrientation();

  switch (currentScreen) {
    case MAIN:
      if (M5.BtnA.wasPressed()) {
        jigglerOn = !jigglerOn;
        saveSettings();
        drawMain();
      }
      if (M5.BtnB.wasPressed()) {
        currentScreen = MENU;
        menuIndex = 0;
        drawMenu();
      }
      break;

    case MENU:
      if (M5.BtnB.wasPressed()) { menuIndex--; if (menuIndex < 0) menuIndex = 5; drawMenu(); }
      if (M5.BtnPWR.wasPressed()) { menuIndex++; if (menuIndex > 5) menuIndex = 0; drawMenu(); }
      if (M5.BtnA.wasPressed()) {
        if (menuIndex == 0) { currentScreen = SET_INTERVAL; drawAdjust("Interval (s)", jiggleInterval / 1000); }
        else if (menuIndex == 1) { currentScreen = SET_DISTANCE; drawAdjust("Distance (px)", jiggleDistance); }
        else if (menuIndex == 2) { currentScreen = SET_SLEEP; drawAdjust("Sleep Timeout (s)", sleepTimeout / 1000); }
        else if (menuIndex == 3) { M5.Display.fillScreen(BLACK); printCentered("Forgetting BLE...", 80, WHITE); delay(2000); ESP.restart(); }
        else if (menuIndex == 4) { M5.Power.powerOff(); }
        else { currentScreen = MAIN; drawMain(); }
      }
      break;

    case SET_INTERVAL:
      if (M5.BtnB.wasPressed()) { jiggleInterval += 1000; if (jiggleInterval > 10000) jiggleInterval = 1000; drawAdjust("Interval (s)", jiggleInterval / 1000); }
      if (M5.BtnPWR.wasPressed()) { jiggleInterval -= 1000; if (jiggleInterval < 1000) jiggleInterval = 10000; drawAdjust("Interval (s)", jiggleInterval / 1000); }
      if (M5.BtnA.wasPressed()) { saveSettings(); showSavedPopup(); currentScreen = MAIN; drawMain(); }
      break;

    case SET_DISTANCE:
      if (M5.BtnB.wasPressed()) { jiggleDistance++; if (jiggleDistance > 20) jiggleDistance = 1; drawAdjust("Distance (px)", jiggleDistance); }
      if (M5.BtnPWR.wasPressed()) { jiggleDistance--; if (jiggleDistance < 1) jiggleDistance = 20; drawAdjust("Distance (px)", jiggleDistance); }
      if (M5.BtnA.wasPressed()) { saveSettings(); showSavedPopup(); currentScreen = MAIN; drawMain(); }
      break;

    case SET_SLEEP:
      if (M5.BtnB.wasPressed()) { sleepTimeout += 60000; if (sleepTimeout > 1800000) sleepTimeout = 0; drawAdjust("Sleep Timeout (s)", sleepTimeout / 1000); }
      if (M5.BtnPWR.wasPressed()) { sleepTimeout -= 60000; if (sleepTimeout < 0) sleepTimeout = 1800000; drawAdjust("Sleep Timeout (s)", sleepTimeout / 1000); }
      if (M5.BtnA.wasPressed()) { saveSettings(); showSavedPopup(); currentScreen = MAIN; drawMain(); }
      break;
  }

  // Jiggle logic
  if (jigglerOn && bleMouse.isConnected() && currentScreen == MAIN) {
    if (millis() - lastJiggle >= jiggleInterval) {
      bleMouse.move(jiggleDistance, 0);
      delay(50);
      bleMouse.move(-jiggleDistance, 0);
      lastJiggle = millis();

      // Circle flash
      heartBeatActive = true;
      heartBeatTimestamp = millis();
      drawStatusBar();
    }
  }

  // Return circle to grey after 200ms
  if (heartBeatActive && millis() - heartBeatTimestamp > 200) {
    heartBeatActive = false;
    if (currentScreen == MAIN) drawStatusBar();
  }

  // Auto-sleep only when OFF
  if (!jigglerOn && currentScreen == MAIN && sleepTimeout > 0) {
    if (millis() - lastActivity >= sleepTimeout) {
      M5.Power.powerOff();
    }
  }

  delay(10);
}
