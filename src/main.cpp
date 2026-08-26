/**
 * Agency Desk Status Cube
 * M5Stack AtomS3 (ESP32-S3FN8, 128x128 GC9107)
 * 
 * Polls https://agency.nabla.net/health/ every ~10s
 * Shows status: ONLINE (green), RESTARTING (yellow), ERROR (red)
 * 
 * Button (GPIO41 / screen):
 *   - Click: force immediate poll
 *   - Hold 2s: forget WiFi, re-enter AP mode
 */

#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiManager.h>

// Optional compile-time WiFi credentials
#if __has_include("secrets.h")
  #include "secrets.h"
#endif

// ─────────────────────────────────────────────────────────────
// Configuration
// ─────────────────────────────────────────────────────────────
static const char* HEALTH_URL = "https://agency.nabla.net/health/";
static const char* AP_NAME = "Agency-Atom";
static const uint32_t POLL_INTERVAL_MS = 10000;
static const uint32_t RESTARTING_DURATION_MS = 30000;
static const uint32_t LONG_PRESS_MS = 2000;
static const uint32_t HTTP_TIMEOUT_MS = 8000;

// ─────────────────────────────────────────────────────────────
// State machine
// ─────────────────────────────────────────────────────────────
enum AgencyState {
  STATE_WIFI_CONFIG,
  STATE_ONLINE,
  STATE_RESTARTING,
  STATE_ERROR
};

static AgencyState currentState = STATE_WIFI_CONFIG;
static String lastBootId = "";
static String currentBootId = "";
static uint32_t restartingStartMs = 0;
static uint32_t lastPollMs = 0;
static bool forceRefresh = true;
static int lastRssi = -100;

// Button state
static uint32_t btnPressStartMs = 0;
static bool btnWasPressed = false;

// Preferences for NVS
Preferences prefs;

// WiFiManager
WiFiManager wm;

// ─────────────────────────────────────────────────────────────
// Screen drawing
// ─────────────────────────────────────────────────────────────
static const uint16_t COLOR_GREEN  = 0x07E0;
static const uint16_t COLOR_YELLOW = 0xFFE0;
static const uint16_t COLOR_RED    = 0xF800;
static const uint16_t COLOR_WHITE  = 0xFFFF;
static const uint16_t COLOR_BLACK  = 0x0000;
static const uint16_t COLOR_GRAY   = 0x7BEF;

void drawRssi(int rssi) {
  M5.Display.setTextColor(COLOR_GRAY, COLOR_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(2, 2);
  M5.Display.printf("%ddB", rssi);
}

void drawCenteredText(const char* text, uint16_t color) {
  M5.Display.setTextColor(color, COLOR_BLACK);
  M5.Display.setTextSize(2);
  int16_t tw = M5.Display.textWidth(text);
  int16_t x = (128 - tw) / 2;
  M5.Display.setCursor(x, 100);
  M5.Display.print(text);
}

void drawOnline() {
  M5.Display.fillScreen(COLOR_BLACK);
  
  // Big green filled circle with checkmark
  int cx = 64, cy = 45, r = 35;
  M5.Display.fillCircle(cx, cy, r, COLOR_GREEN);
  
  // Draw checkmark inside (white)
  // Checkmark points: start, middle, end
  M5.Display.drawLine(cx - 15, cy, cx - 5, cy + 12, COLOR_WHITE);
  M5.Display.drawLine(cx - 5, cy + 12, cx + 18, cy - 12, COLOR_WHITE);
  M5.Display.drawLine(cx - 14, cy, cx - 4, cy + 12, COLOR_WHITE);
  M5.Display.drawLine(cx - 4, cy + 12, cx + 19, cy - 12, COLOR_WHITE);
  
  drawCenteredText("online", COLOR_GREEN);
  drawRssi(lastRssi);
}

void drawRestarting() {
  M5.Display.fillScreen(COLOR_BLACK);
  
  // Yellow circular arrows (simplified as rotating arc segments)
  int cx = 64, cy = 45, r = 30;
  
  // Draw two arc segments to represent rotation
  for (int i = 0; i < 3; i++) {
    int angle1 = i * 120;
    int angle2 = angle1 + 80;
    M5.Display.drawArc(cx, cy, r + 5, r - 5, angle1, angle2, COLOR_YELLOW);
  }
  
  // Arrow heads (triangles at arc ends)
  // Simplified: draw small triangles
  M5.Display.fillTriangle(cx + 28, cy - 18, cx + 35, cy - 8, cx + 22, cy - 8, COLOR_YELLOW);
  M5.Display.fillTriangle(cx - 28, cy + 18, cx - 35, cy + 8, cx - 22, cy + 8, COLOR_YELLOW);
  
  drawCenteredText("reinicio", COLOR_YELLOW);
  drawRssi(lastRssi);
}

void drawError() {
  M5.Display.fillScreen(COLOR_BLACK);
  
  // Big red X
  int cx = 64, cy = 45;
  int s = 30;
  
  // Draw thick X lines
  for (int i = -3; i <= 3; i++) {
    M5.Display.drawLine(cx - s + i, cy - s, cx + s + i, cy + s, COLOR_RED);
    M5.Display.drawLine(cx + s + i, cy - s, cx - s + i, cy + s, COLOR_RED);
  }
  
  drawCenteredText("error", COLOR_RED);
  drawRssi(lastRssi);
}

void drawWifiConfig() {
  M5.Display.fillScreen(COLOR_BLACK);
  
  // WiFi icon (simplified arcs)
  int cx = 64, cy = 50;
  M5.Display.drawArc(cx, cy + 20, 45, 40, 225, 315, COLOR_YELLOW);
  M5.Display.drawArc(cx, cy + 20, 30, 25, 225, 315, COLOR_YELLOW);
  M5.Display.drawArc(cx, cy + 20, 15, 10, 225, 315, COLOR_YELLOW);
  M5.Display.fillCircle(cx, cy + 20, 5, COLOR_YELLOW);
  
  drawCenteredText("wifi", COLOR_YELLOW);
}

void drawConnecting() {
  M5.Display.fillScreen(COLOR_BLACK);
  M5.Display.setTextColor(COLOR_WHITE, COLOR_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(20, 55);
  M5.Display.print("Conectando...");
}

void updateDisplay() {
  switch (currentState) {
    case STATE_WIFI_CONFIG:
      drawWifiConfig();
      break;
    case STATE_ONLINE:
      drawOnline();
      break;
    case STATE_RESTARTING:
      drawRestarting();
      break;
    case STATE_ERROR:
      drawError();
      break;
  }
}

// ─────────────────────────────────────────────────────────────
// WiFi management
// ─────────────────────────────────────────────────────────────
void forgetWiFiAndRestart() {
  Serial.println("Forgetting WiFi credentials...");
  wm.resetSettings();
  delay(500);
  ESP.restart();
}

bool connectWiFi() {
  // Try compile-time credentials first if defined
  #if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
  Serial.println("Trying compile-time WiFi credentials...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(250);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected with compile-time credentials");
    return true;
  }
  Serial.println("\nCompile-time credentials failed, trying WiFiManager...");
  #endif

  // WiFiManager: auto-connect or start AP
  currentState = STATE_WIFI_CONFIG;
  updateDisplay();
  
  wm.setConfigPortalTimeout(0);  // No timeout, wait for config
  wm.setConnectTimeout(20);
  wm.setDebugOutput(true);
  
  // Try to connect with saved credentials, else start AP
  if (!wm.autoConnect(AP_NAME)) {
    Serial.println("WiFiManager failed to connect");
    return false;
  }
  
  return true;
}

// ─────────────────────────────────────────────────────────────
// Health check
// ─────────────────────────────────────────────────────────────
bool parseHealthJson(const String& body, bool& ok, String& bootId) {
  // Minimal JSON parsing without ArduinoJson to save RAM
  // Expected: {"ok": true, "boot_id": "...", "ui_build": "..."}
  
  int okIdx = body.indexOf("\"ok\"");
  if (okIdx < 0) return false;
  
  int colonIdx = body.indexOf(':', okIdx);
  if (colonIdx < 0) return false;
  
  String okStr = body.substring(colonIdx + 1, colonIdx + 10);
  okStr.trim();
  ok = okStr.startsWith("true");
  
  int bootIdIdx = body.indexOf("\"boot_id\"");
  if (bootIdIdx < 0) return false;
  
  int q1 = body.indexOf(':', bootIdIdx);
  if (q1 < 0) return false;
  
  int q2 = body.indexOf('"', q1 + 1);
  if (q2 < 0) return false;
  
  int q3 = body.indexOf('"', q2 + 1);
  if (q3 < 0) return false;
  
  bootId = body.substring(q2 + 1, q3);
  return true;
}

void doHealthCheck() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping health check");
    currentState = STATE_ERROR;
    forceRefresh = true;
    return;
  }
  
  lastRssi = WiFi.RSSI();
  
  WiFiClientSecure client;
  // Use setInsecure() to skip certificate verification
  // ESP32 cert bundle adds ~150KB to flash and complexity
  // For a status display polling a known endpoint, this is acceptable
  client.setInsecure();
  
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  
  Serial.printf("GET %s\n", HEALTH_URL);
  
  if (!http.begin(client, HEALTH_URL)) {
    Serial.println("HTTP begin failed (DNS/TLS)");
    currentState = STATE_ERROR;
    forceRefresh = true;
    http.end();
    return;
  }
  
  int httpCode = http.GET();
  
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP error: %d\n", httpCode);
    currentState = STATE_ERROR;
    forceRefresh = true;
    http.end();
    return;
  }
  
  String body = http.getString();
  http.end();
  
  Serial.printf("Response: %s\n", body.c_str());
  
  bool ok;
  String bootId;
  if (!parseHealthJson(body, ok, bootId)) {
    Serial.println("JSON parse failed");
    currentState = STATE_ERROR;
    forceRefresh = true;
    return;
  }
  
  if (!ok) {
    Serial.println("ok=false");
    currentState = STATE_ERROR;
    forceRefresh = true;
    return;
  }
  
  // Check boot_id
  currentBootId = bootId;
  
  if (lastBootId.isEmpty()) {
    // First successful poll
    lastBootId = bootId;
    currentState = STATE_ONLINE;
    forceRefresh = true;
    Serial.printf("Initial boot_id: %s\n", bootId.c_str());
  } else if (bootId != lastBootId) {
    // Boot ID changed - server restarted
    Serial.printf("Boot ID changed: %s -> %s\n", lastBootId.c_str(), bootId.c_str());
    lastBootId = bootId;
    currentState = STATE_RESTARTING;
    restartingStartMs = millis();
    forceRefresh = true;
  } else {
    // Same boot_id
    if (currentState == STATE_RESTARTING) {
      // Check if we've been in RESTARTING long enough
      if (millis() - restartingStartMs >= RESTARTING_DURATION_MS) {
        currentState = STATE_ONLINE;
        forceRefresh = true;
      }
    } else if (currentState != STATE_ONLINE) {
      // Recovered from error
      currentState = STATE_ONLINE;
      forceRefresh = true;
    }
  }
}

// ─────────────────────────────────────────────────────────────
// Button handling
// ─────────────────────────────────────────────────────────────
void handleButton() {
  M5.update();
  
  if (M5.BtnA.wasPressed()) {
    btnPressStartMs = millis();
    btnWasPressed = true;
    Serial.println("Button pressed");
  }
  
  if (btnWasPressed && M5.BtnA.isPressed()) {
    if (millis() - btnPressStartMs >= LONG_PRESS_MS) {
      // Long press detected
      Serial.println("Long press - forgetting WiFi");
      btnWasPressed = false;
      forgetWiFiAndRestart();
    }
  }
  
  if (M5.BtnA.wasReleased()) {
    if (btnWasPressed && millis() - btnPressStartMs < LONG_PRESS_MS) {
      // Short click - force poll
      Serial.println("Short click - forcing poll");
      lastPollMs = 0;  // Force immediate poll
    }
    btnWasPressed = false;
  }
}

// ─────────────────────────────────────────────────────────────
// Setup & Loop
// ─────────────────────────────────────────────────────────────
void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  
  Serial.println("\n\n=== Agency Desk Status Cube ===");
  Serial.println("M5Stack AtomS3");
  
  M5.Display.setRotation(0);
  M5.Display.setBrightness(80);
  M5.Display.fillScreen(COLOR_BLACK);
  
  drawConnecting();
  
  if (!connectWiFi()) {
    Serial.println("WiFi connection failed");
    currentState = STATE_ERROR;
    updateDisplay();
  } else {
    Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    lastRssi = WiFi.RSSI();
    currentState = STATE_ONLINE;
    forceRefresh = true;
    // Force immediate first poll
    lastPollMs = 0;
  }
}

void loop() {
  handleButton();
  
  // WiFi reconnection
  if (WiFi.status() != WL_CONNECTED && currentState != STATE_WIFI_CONFIG) {
    Serial.println("WiFi disconnected, reconnecting...");
    drawConnecting();
    WiFi.reconnect();
    delay(5000);
    if (WiFi.status() != WL_CONNECTED) {
      currentState = STATE_ERROR;
      forceRefresh = true;
    }
  }
  
  // Periodic health check
  uint32_t now = millis();
  if (now - lastPollMs >= POLL_INTERVAL_MS || lastPollMs == 0) {
    lastPollMs = now;
    AgencyState prevState = currentState;
    doHealthCheck();
    if (currentState != prevState || forceRefresh) {
      updateDisplay();
      forceRefresh = false;
    }
  }
  
  // Check RESTARTING timeout
  if (currentState == STATE_RESTARTING) {
    if (millis() - restartingStartMs >= RESTARTING_DURATION_MS) {
      currentState = STATE_ONLINE;
      updateDisplay();
    }
  }
  
  delay(50);
}
