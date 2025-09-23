// ============================================================
// MCUwifiSimplified_Autonomous.ino
//
// ESP32 bridge for Rover project.
// - Runs WiFi AP + UDP server
// - Forwards controller commands to VehicleManager Nano
// - Forwards SensorManager telemetry to controller
// - Adds autonomous mode with obstacle avoidance & speed regulation
//
// Autonomous mode behavior:
//   * If obstacles <100 cm ahead, choose clearest path (left/right/straight)
//   * If no forward path, use rear sensors to back up with a turn
//   * Regulates throttle to maintain ~50% target speed
// ============================================================

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ctype.h>

// =========================
// WiFi Config
// =========================
const char* ssid     = "Robot_AP";
const char* password = "12345678";

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

#define TELEMETRY_BUFFER_SIZE 256
char telemetryBuffer[TELEMETRY_BUFFER_SIZE];
size_t telemetryIndex = 0;

// =========================
// Watchdog
// =========================
unsigned long lastPacketTime = 0;
const unsigned long failsafeTimeout = 1000; // ms
bool failsafeActive = false;

// =========================
// Autonomous mode settings
// =========================
bool autonomousMode = false;   // Enabled via controller AUTO:1; command
int obstacleThreshold = 100;   // cm
int targetSpeed = 50;          // percent of max

// Latest telemetry values
// Expected format: S:front,frontLeft,frontRight,rearLeft,rearRight,temp,hum,speed;
int distances[5] = {0};  // F, FL, FR, RL, RR
int speed = 0;

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

  if (autonomousMode) {
    autonomousControl();
  } else {
    checkFailsafe();
  }
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

    // Check for AUTO mode toggle
    if (strstr(packetBuffer, "AUTO:1") != NULL) {
      autonomousMode = true;
      Serial.println("Autonomous mode ENABLED");
    } else if (strstr(packetBuffer, "AUTO:0") != NULL) {
      autonomousMode = false;
      Serial.println("Autonomous mode DISABLED");
    }

    // If not in autonomous, forward controller command directly
    if (!autonomousMode) {
      nano2.print(packetBuffer);
      nano2.print("\n");
    }
  }
}

// =========================
// Forward telemetry from Sensor Manager to remote
// =========================
void forwardTelemetry() {
  while (nano1.available()) {
    char c = nano1.read();

    if (c == '\n') {
      telemetryBuffer[telemetryIndex] = '\0';

      String telemetry = String(telemetryBuffer);
      telemetry.trim();

      if (telemetry.startsWith("S:")) {
        parseTelemetry(telemetry);
      }

      // Forward telemetry unchanged to controller
      if (remoteIP) {
        udp.beginPacket(remoteIP, remotePort);
        udp.print(telemetry);
        udp.endPacket();
      }

      Serial.print("Forwarded telemetry: ");
      Serial.println(telemetry);

      telemetryIndex = 0;
    } else if (c != '\r') {
      if (telemetryIndex < TELEMETRY_BUFFER_SIZE - 1) {
        telemetryBuffer[telemetryIndex++] = c;
      }
    }
  }
}

// =========================
// Parse telemetry string
// Expected: S:front,frontLeft,frontRight,rearLeft,rearRight,temp,hum,speed;
// =========================
void parseTelemetry(String line) {
  line.remove(0, 2); // remove "S:"
  int parts[8];
  int idx = 0;
  char buf[128];
  line.toCharArray(buf, sizeof(buf));
  char* tok = strtok(buf, ",;");
  while (tok && idx < 8) {
    parts[idx++] = atoi(tok);
    tok = strtok(NULL, ",;");
  }
  if (idx >= 5) {
    for (int i = 0; i < 5; i++) distances[i] = parts[i];
    if (idx >= 8) speed = parts[7];
  }
}

// =========================
// Autonomous decision logic
// =========================
void autonomousControl() {
  int steer = 90;
  int throttle = 90;

  bool frontBlocked  = (distances[0] > 0 && distances[0] < obstacleThreshold);
  bool leftBlocked   = (distances[1] > 0 && distances[1] < obstacleThreshold);
  bool rightBlocked  = (distances[2] > 0 && distances[2] < obstacleThreshold);

  // If all forward paths blocked -> reverse recovery
  if (frontBlocked && leftBlocked && rightBlocked) {
    Serial.println("All forward paths blocked - reversing!");

    int rearLeft  = distances[3];
    int rearRight = distances[4];

    throttle = 70; // reverse
    if (rearLeft > rearRight) {
      steer = 60; // back while turning left
    } else {
      steer = 120; // back while turning right
    }

  } else {
    // At least one forward path open
    if (!frontBlocked) {
      steer = 90; // straight
    } else if (!leftBlocked && rightBlocked) {
      steer = 60; // turn left
    } else if (!rightBlocked && leftBlocked) {
      steer = 120; // turn right
    } else {
      // Both sides open -> choose wider clearance
      if (distances[1] > distances[2]) steer = 60;
      else steer = 120;
    }

    // Speed regulation for forward
    int desired = targetSpeed;
    if (speed < desired) {
      throttle = 120; // more power
    } else if (speed > desired) {
      throttle = 80;  // less power
    } else {
      throttle = 100; // maintain
    }
  }

  // Send command to VehicleManager Nano
  String cmd = "";
  cmd += "STEER:" + String(steer) + ";";
  cmd += "THROT:" + String(throttle) + ";";
  cmd += "HORN:0;LIGHTS:0;AUTO:1;";
  nano2.println(cmd);

  Serial.print("Auto cmd -> ");
  Serial.println(cmd);
}

// =========================
// Failsafe handling
// =========================
void checkFailsafe() {
  if (millis() - lastPacketTime > failsafeTimeout && !failsafeActive) {
    Serial.println("Failsafe triggered!");
    failsafeActive = true;
    nano2.println("CMD FAILSAFE");
    nano2.flush();
  }
}
