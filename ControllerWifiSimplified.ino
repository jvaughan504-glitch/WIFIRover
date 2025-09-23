// WIFI Controller v1
#include <WiFi.h>
#include <WiFiUdp.h>
#include <TFT_eSPI.h>
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
TFT_eSPI tft = TFT_eSPI();

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
void drawDistanceLine(const char* label, float value) {
  if (value < 0) {
    tft.printf("%s: ---\n", label);
  } else {
    tft.printf("%s: %.1f cm\n", label, value);
  }
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.println("Controller Booting...");

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  lastLightsButtonState = digitalRead(BTN2);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
  }

  udp.begin(robotPort);
  tft.println("WiFi Connected");
  tft.print("IP: ");
  tft.println(WiFi.localIP());
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
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.println("Telemetry:");

  const char* firstColon = strchr(telemetry, ':');
  if (!firstColon) {
    tft.println("No data");
    return;
  }

  const char* payloadStart = firstColon + 1;
  size_t payloadLength = strlen(payloadStart);
  if (payloadLength == 0) {
    tft.println("No data");
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
    tft.println("Invalid telemetry");
    return;
  }

  drawDistanceLine("FL", values[0]);
  drawDistanceLine("FR", values[1]);
  drawDistanceLine("RL", values[2]);
  drawDistanceLine("RR", values[3]);
  tft.printf("Speed: %.2f m/s\n", values[4]);
  tft.printf("Dist: %.2f m\n", values[5]);
}
