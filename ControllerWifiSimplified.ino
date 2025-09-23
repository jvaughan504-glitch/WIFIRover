// WIFI Controller v1
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

// =========================
// Control values
// =========================
int steer = 90;
int throttle = 90;
bool horn = false;
bool lights = false;
bool lastLightsButtonState = HIGH;

// =========================
// Timing
// =========================
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 100; // ms

// =========================
// Helpers
// =========================
void drawDistanceValue(int16_t x, int16_t y, const char* label, float value) {
  oled.setCursor(x, y);
  oled.print(label);
  oled.print(F(": "));
  if (value < 0) {
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
}

void sendControl() {
  unsigned long now = millis();
  if (now - lastSendTime < sendInterval) {
    return;
  }
  lastSendTime = now;

  char cmd[64];
  int length = snprintf(cmd, sizeof(cmd),
                        "STEER:%d;THROT:%d;HORN:%d;LIGHTS:%d;AUTO:0;",
                        steer,
                        throttle,
                        horn ? 1 : 0,
                        lights ? 1 : 0);
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

  const char* firstColon = strchr(telemetry, ':');
  if (!firstColon) {
    oled.setCursor(0, 0);
    oled.println(F("No data"));
    oled.display();
    return;
  }

  const char* payloadStart = firstColon + 1;
  size_t payloadLength = strlen(payloadStart);
  if (payloadLength == 0) {
    oled.setCursor(0, 0);
    oled.println(F("No data"));
    oled.display();
    return;
  }

  char payload[128];
  if (payloadLength >= sizeof(payload)) {
    payloadLength = sizeof(payload) - 1;
  }
  memcpy(payload, payloadStart, payloadLength);
  payload[payloadLength] = '\0';

  char* endMarker = strrchr(payload, ';');
  if (endMarker != nullptr) {
    *endMarker = '\0';
  }

  float values[6] = {0};
  uint8_t valueCount = 0;
  char* savePtr = nullptr;
  char* token = strtok_r(payload, ",", &savePtr);
  while (token != nullptr && valueCount < 6) {
    values[valueCount++] = atof(token);
    token = strtok_r(nullptr, ",", &savePtr);
  }

  if (valueCount < 6) {
    oled.setCursor(0, 0);
    oled.println(F("Invalid"));
    oled.display();
    return;
  }

  const int16_t leftX = 0;
  const int16_t rightX = SCREEN_WIDTH / 2;
  const int16_t lineHeight = 8;
  const int16_t metricStartY = lineHeight * 3;
  const int16_t metricSpacing = lineHeight + 2;

  oled.setCursor(leftX, 0);
  oled.println(F("Range (cm)"));

  drawDistanceValue(leftX, lineHeight, "FL", values[0]);
  drawDistanceValue(rightX, lineHeight, "FR", values[1]);
  drawDistanceValue(leftX, lineHeight * 2, "RL", values[2]);
  drawDistanceValue(rightX, lineHeight * 2, "RR", values[3]);

  oled.setCursor(leftX, metricStartY);
  oled.print(F("Speed: "));
  if (values[4] < 0) {
    oled.print(F("--.--"));
  } else {
    oled.print(values[4], 2);
  }
  oled.print(F(" m/s"));

  oled.setCursor(leftX, metricStartY + metricSpacing);
  oled.print(F("Distance: "));
  if (values[5] < 0) {
    oled.print(F("--.--"));
  } else {
    oled.print(values[5], 2);
  }
  oled.print(F(" m"));

  oled.display();
}
