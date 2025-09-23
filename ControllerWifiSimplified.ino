// WIFI Controller v1
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

// =========================
// WiFi Config
// =========================
const char* ssid     = "Robot_AP";
const char* password = "12345678";
IPAddress robotIP(192, 168, 4, 1);
const int robotPort = 4210;
WiFiUDP udp;

// =========================
// Display
// =========================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_I2C_ADDRESS 0x3D  // Default I2C address for the 128x64 Micro OLED
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// =========================
// Joystick & Buttons
// =========================
#define JOY_X 34
#define JOY_Y 35
#define BTN1  0
#define BTN2  2
#define BTN_AUTO_ENABLE  32
#define BTN_AUTO_DISABLE 33

// =========================
// Control values
// =========================
int steer = 90;
int throttle = 90;
bool horn = false;
bool lights = false;
bool lastLightsButtonState = HIGH;
bool autoModeEnabled = false;

// =========================
// Timing
// =========================
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 100; // ms
const unsigned long autoButtonHoldMs = 1000; // ms required for AUTO button long press

// =========================
// Helpers
// =========================
void drawDistanceValue(int16_t x, int16_t y, const char* label, float value) {
  oled.setCursor(x, y);
  oled.print(label);
  oled.print(F(": "));
  if (isnan(value) || value < 0.0f) {
    oled.print(F("--.-"));
  } else {
    oled.print(value, 1);
  }
}

void setup() {
  Serial.begin(115200);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
    Serial.println(F("SSD1306 init failed"));
    while (true) {
      delay(10);
    }
  }
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setTextWrap(false);
  oled.setCursor(0, 0);
  oled.println(F("Controller"));
  oled.println(F("Booting..."));
  oled.println(F("Connecting..."));
  oled.display();

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN_AUTO_ENABLE, INPUT_PULLUP);
  pinMode(BTN_AUTO_DISABLE, INPUT_PULLUP);
  lastLightsButtonState = digitalRead(BTN2);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
  }

  udp.begin(robotPort);
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.println(F("WiFi Ready"));
  oled.print(F("IP:"));
  oled.println(WiFi.localIP());
  oled.display();
}

void loop() {
  readInputs();
  sendControl();
  readTelemetry();
}

void readInputs() {
  int x = analogRead(JOY_X);
  int y = analogRead(JOY_Y);

  steer = map(x, 0, 4095, 0, 180);
  steer = constrain(steer, 0, 180);

  throttle = map(y, 0, 4095, 180, 0); // forward = larger throttle value
  throttle = constrain(throttle, 0, 180);

  horn = (digitalRead(BTN1) == LOW);

  bool currentLightsButton = digitalRead(BTN2);
  if (lastLightsButtonState == HIGH && currentLightsButton == LOW) {
    lights = !lights;
  }
  lastLightsButtonState = currentLightsButton;

  bool autoEnableButton = (digitalRead(BTN_AUTO_ENABLE) == LOW);
  bool autoDisableButton = (digitalRead(BTN_AUTO_DISABLE) == LOW);
  static bool enablePressActive = false;
  static bool disablePressActive = false;
  static bool enableActionLatched = false;
  static bool disableActionLatched = false;
  static unsigned long enablePressStart = 0;
  static unsigned long disablePressStart = 0;

  unsigned long now = millis();

  if (autoEnableButton) {
    if (!enablePressActive) {
      enablePressActive = true;
      enableActionLatched = false;
      enablePressStart = now;
    } else if (!enableActionLatched && (now - enablePressStart) >= autoButtonHoldMs) {
      if (!autoModeEnabled) {
        autoModeEnabled = true;
        Serial.println(F("AUTO mode requested -> ENABLE"));
      }
      enableActionLatched = true;
    }
  } else {
    enablePressActive = false;
    enableActionLatched = false;
  }

  if (autoDisableButton) {
    if (!disablePressActive) {
      disablePressActive = true;
      disableActionLatched = false;
      disablePressStart = now;
    } else if (!disableActionLatched && (now - disablePressStart) >= autoButtonHoldMs) {
      if (autoModeEnabled) {
        autoModeEnabled = false;
        Serial.println(F("AUTO mode requested -> DISABLE"));
      }
      disableActionLatched = true;
    }
  } else {
    disablePressActive = false;
    disableActionLatched = false;
  }
}

void sendControl() {
  unsigned long now = millis();
  if (now - lastSendTime < sendInterval) {
    return;
  }
  lastSendTime = now;

  char cmd[64];
  int length = snprintf(cmd, sizeof(cmd),
                        "STEER:%d;THROT:%d;HORN:%d;LIGHTS:%d;AUTO:%d;",
                        steer,
                        throttle,
                        horn ? 1 : 0,
                        lights ? 1 : 0,
                        autoModeEnabled ? 1 : 0);
  if (length < 0) {
    return;
  }
  if (length >= static_cast<int>(sizeof(cmd))) {
    length = sizeof(cmd) - 1;
  }

  udp.beginPacket(robotIP, robotPort);
  udp.write(reinterpret_cast<const uint8_t*>(cmd), length);
  udp.endPacket();
}

void readTelemetry() {
  int packetSize = udp.parsePacket();
  if (packetSize <= 0) {
    return;
  }

  char buffer[256];
  int len = udp.read(buffer, sizeof(buffer) - 1);
  if (len <= 0) {
    return;
  }
  buffer[len] = '\0';

  displayTelemetry(buffer);
}

void displayTelemetry(const char* telemetry) {
  oled.clearDisplay();

  const int16_t leftX = 0;
  const int16_t rightX = SCREEN_WIDTH / 2;
  const int16_t lineHeight = 8;

  oled.setCursor(leftX, 0);
  oled.print(F("Auto: "));
  oled.print(autoModeEnabled ? F("ON") : F("OFF"));

  const char* firstColon = strchr(telemetry, ':');
  if (!firstColon) {
    oled.setCursor(leftX, lineHeight);
    oled.println(F("No data"));
    oled.display();
    return;
  }

  const char* payloadStart = firstColon + 1;
  size_t payloadLength = strlen(payloadStart);
  if (payloadLength == 0) {
    oled.setCursor(leftX, lineHeight);
    oled.println(F("No data"));
    oled.display();
    return;
  }

  char payload[160];
  if (payloadLength >= sizeof(payload)) {
    payloadLength = sizeof(payload) - 1;
  }
  memcpy(payload, payloadStart, payloadLength);
  payload[payloadLength] = '\0';

  char* endMarker = strrchr(payload, ';');
  if (endMarker != nullptr) {
    *endMarker = '\0';
  }

  float values[8];
  bool hasValue[8];
  for (int i = 0; i < 8; ++i) {
    values[i] = NAN;
    hasValue[i] = false;
  }

  auto trimToken = [](char* token) {
    while (token && *token && isspace(static_cast<unsigned char>(*token))) {
      ++token;
    }
    if (!token) {
      return token;
    }
    size_t len = strlen(token);
    while (len > 0 && isspace(static_cast<unsigned char>(token[len - 1]))) {
      token[--len] = '\0';
    }
    return token;
  };

  auto isNullToken = [](const char* token) {
    if (!token) {
      return true;
    }
    size_t len = strlen(token);
    if (len != 4) {
      return false;
    }
    return (tolower(static_cast<unsigned char>(token[0])) == 'n' &&
            tolower(static_cast<unsigned char>(token[1])) == 'u' &&
            tolower(static_cast<unsigned char>(token[2])) == 'l' &&
            tolower(static_cast<unsigned char>(token[3])) == 'l');
  };

  uint8_t valueCount = 0;
  char* savePtr = nullptr;
  char* token = strtok_r(payload, ",", &savePtr);
  while (token != nullptr && valueCount < 8) {
    token = trimToken(token);
    if (!token || token[0] == '\0' || isNullToken(token)) {
      values[valueCount] = NAN;
      hasValue[valueCount] = false;
    } else {
      char* endPtr = nullptr;
      float v = strtof(token, &endPtr);
      if (endPtr != nullptr) {
        while (*endPtr && isspace(static_cast<unsigned char>(*endPtr))) {
          ++endPtr;
        }
      }
      if (endPtr != nullptr && *endPtr == '\0' && !isnan(v)) {
        values[valueCount] = v;
        hasValue[valueCount] = true;
      } else {
        values[valueCount] = NAN;
        hasValue[valueCount] = false;
      }
    }

    ++valueCount;
    token = strtok_r(nullptr, ",", &savePtr);
  }

  if (valueCount < 8) {
    oled.setCursor(leftX, lineHeight);
    oled.println(F("Telemetry err"));
    oled.display();
    return;
  }

  float front = hasValue[0] ? values[0] : NAN;
  float frontLeft = hasValue[1] ? values[1] : NAN;
  float frontRight = hasValue[2] ? values[2] : NAN;
  float rearLeft = hasValue[3] ? values[3] : NAN;
  float rearRight = hasValue[4] ? values[4] : NAN;
  float tempC = hasValue[5] ? values[5] : NAN;
  float humidityPct = hasValue[6] ? values[6] : NAN;
  float speedMps = hasValue[7] ? values[7] : NAN;

  drawDistanceValue(rightX, 0, "F", front);

  oled.setCursor(leftX, lineHeight);
  oled.println(F("Range (cm)"));

  drawDistanceValue(leftX, lineHeight * 2, "FL", frontLeft);
  drawDistanceValue(rightX, lineHeight * 2, "FR", frontRight);
  drawDistanceValue(leftX, lineHeight * 3, "RL", rearLeft);
  drawDistanceValue(rightX, lineHeight * 3, "RR", rearRight);

  const int16_t metricStartY = lineHeight * 4;
  const int16_t metricSpacing = lineHeight + 2;

  oled.setCursor(leftX, metricStartY);
  oled.print(F("Speed: "));
  if (!hasValue[7] || isnan(speedMps)) {
    oled.print(F("--.--"));
  } else {
    oled.print(speedMps, 2);
  }
  oled.print(F(" m/s"));

  oled.setCursor(leftX, metricStartY + metricSpacing);
  oled.print(F("Temp: "));
  if (!hasValue[5] || isnan(tempC)) {
    oled.print(F("--.-"));
  } else {
    oled.print(tempC, 1);
  }
  oled.print(F(" C"));

  oled.setCursor(leftX, metricStartY + metricSpacing * 2);
  oled.print(F("Hum: "));
  if (!hasValue[6] || isnan(humidityPct)) {
    oled.print(F("--.-"));
  } else {
    oled.print(humidityPct, 1);
  }
  oled.print(F(" %"));

  oled.display();
}
