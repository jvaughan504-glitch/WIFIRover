// WIFI Controller v1
#include <WiFi.h>
#include <WiFiUdp.h>
#include <TFT_eSPI.h>

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

  String cmd;
  cmd.reserve(64);
  cmd += "STEER:" + String(steer) + ";";
  cmd += "THROT:" + String(throttle) + ";";
  cmd += "HORN:" + String(horn ? 1 : 0) + ";";
  cmd += "LIGHTS:" + String(lights ? 1 : 0) + ";";
  cmd += "AUTO:0;";

  udp.beginPacket(robotIP, robotPort);
  udp.print(cmd);
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

  displayTelemetry(String(buffer));
}

void displayTelemetry(const String& telemetry) {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.println("Telemetry:");

  int firstColon = telemetry.indexOf(':');
  if (firstColon == -1) {
    tft.println("No data");
    return;
  }

  String payload = telemetry.substring(firstColon + 1);
  payload.trim();
  if (payload.endsWith(";")) {
    payload.remove(payload.length() - 1);
  }

  float values[6] = {0};
  int valueCount = 0;
  int start = 0;
  while (valueCount < 6 && start < payload.length()) {
    int comma = payload.indexOf(',', start);
    if (comma == -1) {
      comma = payload.length();
    }
    String token = payload.substring(start, comma);
    token.trim();
    values[valueCount++] = token.toFloat();
    start = comma + 1;
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
