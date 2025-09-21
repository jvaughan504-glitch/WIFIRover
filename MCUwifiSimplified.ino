//MCUwifiSimplified (Improved)
#include <WiFi.h>
#include <WiFiUdp.h>

// =========================
// WiFi Config
// =========================
const char* ssid     = "Robot_AP";
const char* password = "12345678";

// UDP settings
WiFiUDP udp;
const int localPort = 4210;   // ESP32 listening port
IPAddress remoteIP;           // Controller IP
int remotePort = 4210;        // Controller port

// =========================
// Serial Links
// =========================
// Nano 1 (Sensor Manager)
#define NANO1_RX 18
#define NANO1_TX 19

// Nano 2 (Vehicle Manager)
#define NANO2_RX 16
#define NANO2_TX 17

HardwareSerial nano1(1);  // Sensor Manager
HardwareSerial nano2(2);  // Vehicle Manager

// =========================
// Watchdog
// =========================
unsigned long lastPacketTime = 0;
const unsigned long failsafeTimeout = 1000; // ms
bool failsafeActive = false;

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(115200);

  // Init serial ports
  nano1.begin(115200, SERIAL_8N1, NANO1_RX, NANO1_TX);
  nano2.begin(115200, SERIAL_8N1, NANO2_RX, NANO2_TX);

  // Start WiFi as Access Point
  WiFi.softAP(ssid, password);
  Serial.println("WiFi AP started");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // Start UDP
  udp.begin(localPort);
  Serial.printf("UDP listening on port %d\n", localPort);
}

// =========================
// Main Loop
// =========================
void loop() {
  handleUDP();
  forwardTelemetry();
  checkFailsafe();
}

// =========================
// Handle incoming UDP control packets
// =========================
void handleUDP() {
  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    char packetBuffer[256] = {0};  // clear buffer
    int len = udp.read(packetBuffer, sizeof(packetBuffer) - 1);
    if (len > 0) {
      packetBuffer[len] = '\0';
    }

    // Remember controller IP and port
    remoteIP = udp.remoteIP();
    remotePort = udp.remotePort();
    lastPacketTime = millis();
    failsafeActive = false;

    Serial.print("Received UDP: ");
    Serial.println(packetBuffer);

    // Forward command to Vehicle Manager Nano
    nano2.print(packetBuffer);
    nano2.print("\n");
  }
}

// =========================
// Forward telemetry from Sensor Manager to remote
// =========================
void forwardTelemetry() {
  while (nano1.available()) {
    String telemetry = nano1.readStringUntil('\n');
    telemetry.trim();
    if (telemetry.length() > 0 && remoteIP) {
      udp.beginPacket(remoteIP, remotePort);
      udp.print(telemetry);
      udp.endPacket();

      Serial.print("Forwarded telemetry: ");
      Serial.println(telemetry);
    }
  }
}

// =========================
// Failsafe handling
// =========================
void checkFailsafe() {
  if (millis() - lastPacketTime > failsafeTimeout && !failsafeActive) {
    Serial.println("Failsafe triggered!");
    failsafeActive = true;
    nano2.println("CMD FAILSAFE");
    nano2.flush(); // ensure the command is sent immediately
  }
}
