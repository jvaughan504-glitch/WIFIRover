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
int steer = 0;
int throttle = 0;
bool horn = false;
bool lights = false;

// =========================
// Timing
// =========================
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 100; // ms

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

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(200);

  udp.begin(4210);
  tft.println("WiFi Connected");
  tft.print("IP: "); tft.println(WiFi.localIP());
}

void loop() {
  readInputs();
  sendControl();
  readTelemetry();
}

void readInputs() {
  int x = analogRead(JOY_X);
  int y = analogRead(JOY_Y);
  steer = map(x, 0, 4095, -90, 90);
  throttle = map(y, 0, 4095, 100, -100);
  horn = (digitalRead(BTN1) == LOW);
  if (digitalRead(BTN2) == LOW) { lights = !lights; delay(200); }
}

void sendControl() {
  if (millis() - lastSendTime < sendInterval) return;
  lastSendTime = millis();

  String cmd = "";
  cmd += "CMD STEER " + String(steer) + ";";
  cmd += "CMD THROT " + String(throttle) + ";";
  cmd += "CMD HORN " + String(horn ? 1 : 0) + ";";
  cmd += "CMD LIGHTS " + String(lights ? 1 : 0) + ";";

  udp.beginPacket(robotIP, robotPort);
  udp.print(cmd);
  udp.endPacket();
}

void readTelemetry() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char buffer[256];
    int len = udp.read(buffer, 255);
    if (len > 0) buffer[len] = '\0';
    displayTelemetry(String(buffer));
  }
}

void displayTelemetry(String telemetry) {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.println("Telemetry:");
  int firstColon = telemetry.indexOf(':');
  if (firstColon != -1) {
    String values = telemetry.substring(firstColon + 1);
    values.replace(";", "");
    int data[7], i = 0;
    char *ptr = strtok((char*)values.c_str(), ",");
    while (ptr != NULL && i < 7) { data[i++] = atoi(ptr); ptr = strtok(NULL, ","); }
    if (i >= 7) {
      tft.printf("F: %d cm\n", data[0]);
      tft.printf("FL: %d cm\n", data[1]);
      tft.printf("FR: %d cm\n", data[2]);
      tft.printf("L: %d cm\n", data[3]);
      tft.printf("R: %d cm\n", data[4]);
      tft.printf("T: %d C\n", data[5]);
      tft.printf("H: %d %%\n", data[6]);
    }
  }
}
