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
#include <math.h>
#include <stdlib.h>
#include <string.h>

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
bool autonomousMode = false;          // Enabled via controller AUTO:1; command
float obstacleThresholdCm = 100.0f;   // cm
const float targetSpeedMps = 0.5f;    // desired forward speed (m/s)
const float speedDeadbandMps = 0.05f; // acceptable ± range around target speed

const unsigned long telemetryTimeoutMs = 1000; // ms before considering telemetry stale
unsigned long lastTelemetryUpdate = 0;

// Latest telemetry values
// Format: S:front,frontLeft,frontRight,rearLeft,rearRight,temp,hum,speed;
float frontDistance = NAN;
float frontLeftDistance = NAN;
float frontRightDistance = NAN;
float rearLeftDistance = NAN;
float rearRightDistance = NAN;
float ambientTemperatureC = NAN;
float relativeHumidityPct = NAN;
float speedMps = NAN;

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

  if (autonomousMode && !failsafeActive) {
    autonomousControl();
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
  line.remove(0, 2); // strip "S:"

  char buf[160];
  line.toCharArray(buf, sizeof(buf));

  float parsed[8];
  bool hasValue[8];
  for (int i = 0; i < 8; ++i) {
    parsed[i] = NAN;
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

  char* context = nullptr;
  uint8_t idx = 0;
  char* token = strtok_r(buf, ",;", &context);
  while (token && idx < 8) {
    token = trimToken(token);
    if (!token || token[0] == '\0' || isNullToken(token)) {
      parsed[idx] = NAN;
      hasValue[idx] = false;
    } else {
      char* endPtr = nullptr;
      float value = strtof(token, &endPtr);
      if (endPtr != nullptr) {
        while (*endPtr && isspace(static_cast<unsigned char>(*endPtr))) {
          ++endPtr;
        }
      }
      if (endPtr != nullptr && *endPtr == '\0' && !isnan(value)) {
        parsed[idx] = value;
        hasValue[idx] = true;
      } else {
        parsed[idx] = NAN;
        hasValue[idx] = false;
      }
    }

    ++idx;
    token = strtok_r(nullptr, ",;", &context);
  }

  frontDistance = (idx > 0 && hasValue[0]) ? parsed[0] : NAN;
  frontLeftDistance = (idx > 1 && hasValue[1]) ? parsed[1] : NAN;
  frontRightDistance = (idx > 2 && hasValue[2]) ? parsed[2] : NAN;
  rearLeftDistance = (idx > 3 && hasValue[3]) ? parsed[3] : NAN;
  rearRightDistance = (idx > 4 && hasValue[4]) ? parsed[4] : NAN;
  ambientTemperatureC = (idx > 5 && hasValue[5]) ? parsed[5] : NAN;
  relativeHumidityPct = (idx > 6 && hasValue[6]) ? parsed[6] : NAN;
  speedMps = (idx > 7 && hasValue[7]) ? parsed[7] : NAN;

  if (idx > 0) {
    lastTelemetryUpdate = millis();
  }
}

// =========================
// Autonomous decision logic
// =========================
void autonomousControl() {
  int steer = 90;
  int throttle = 90;

  unsigned long now = millis();
  bool telemetryFresh = (now - lastTelemetryUpdate) <= telemetryTimeoutMs;
  static bool telemetryWarningPrinted = false;

  if (!telemetryFresh) {
    if (!telemetryWarningPrinted) {
      Serial.println("Telemetry stale - holding position");
      telemetryWarningPrinted = true;
    }
  } else {
    telemetryWarningPrinted = false;

    auto isObstacle = [&](float distance) {
      return !isnan(distance) && distance > 0.0f && distance < obstacleThresholdCm;
    };

    auto clearance = [&](float distance) {
      return isnan(distance) ? 0.0f : distance;
    };

    bool leftBlocked = isObstacle(frontLeftDistance);
    bool rightBlocked = isObstacle(frontRightDistance);
    bool forwardBlocked = leftBlocked && rightBlocked; // no dedicated front sensor yet

    if (forwardBlocked) {
      Serial.println("All forward paths blocked - reversing!");

      throttle = 70; // reverse

      float rearLeftClear = clearance(rearLeftDistance);
      float rearRightClear = clearance(rearRightDistance);

      if (isnan(rearLeftDistance) && isnan(rearRightDistance)) {
        steer = 90; // unknown rear clearance -> back straight
      } else if (isnan(rearRightDistance) || rearLeftClear > rearRightClear) {
        steer = 60; // favour the clearer side
      } else {
        steer = 120;
      }
    } else {
      if (!leftBlocked && !rightBlocked) {
        steer = 90; // both sides clear -> go straight
      } else if (leftBlocked && !rightBlocked) {
        steer = 120; // obstacle on the left -> turn right
      } else if (rightBlocked && !leftBlocked) {
        steer = 60;  // obstacle on the right -> turn left
      } else {
        // Both sensors report obstacles but at different ranges -> pick wider gap
        float leftClear = clearance(frontLeftDistance);
        float rightClear = clearance(frontRightDistance);
        steer = (leftClear >= rightClear) ? 60 : 120;
      }

      if (!isnan(speedMps)) {
        if (speedMps < targetSpeedMps - speedDeadbandMps) {
          throttle = 120; // need more speed
        } else if (speedMps > targetSpeedMps + speedDeadbandMps) {
          throttle = 80;  // slow down
        } else {
          throttle = 100; // within deadband -> maintain
        }
      } else {
        throttle = 100; // no speed estimate -> maintain moderate throttle
      }
    }
  }

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
