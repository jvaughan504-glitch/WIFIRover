// ============================================================
// MCUwifiSimplified_Autonomous_WebSocketMobileToggles.ino
//
// ESP32 bridge with WebSocket telemetry & controls
// Mobile-friendly UI with obstacle tiles + Horn/Lights toggles
// ============================================================

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

// =========================
// WiFi Config
// =========================
const char* ssid     = "Robot_AP";
const char* password = "12345678";   // WiFi AP password
const char* webPass  = "rover123";   // Password for Auto mode

WiFiUDP udp;
const int localPort = 4210;
IPAddress remoteIP;
int remotePort = 4210;

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// =========================
// Serial Links
// =========================
#define NANO1_RX 18
#define NANO1_TX 19
#define NANO2_RX 16
#define NANO2_TX 17

HardwareSerial nano1(1);
HardwareSerial nano2(2);

#define TELEMETRY_BUFFER_SIZE 256
char telemetryBuffer[TELEMETRY_BUFFER_SIZE];
size_t telemetryIndex = 0;

// =========================
// State
// =========================
unsigned long lastPacketTime = 0;
const unsigned long failsafeTimeout = 1000;
bool failsafeActive = false;

bool autonomousMode = false;
bool hornOn = false;
bool lightsOn = false;

float obstacleThreshold = 100.0f;
float targetSpeed = 0.6f; // metres per second

float distances[5] = {NAN, NAN, NAN, NAN, NAN};
float temp = NAN;
float hum = NAN;
float speed = NAN;

// =========================
// Web page (with toggle buttons)
// =========================
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>Rover Dashboard</title>
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<style>
body { font-family:Arial, sans-serif; background:#111; color:#eee; margin:0; padding:15px; }
h2 { color:#6f6; text-align:center; }
.status { text-align:center; font-size:1.5em; margin:10px 0; }
.status.auto { color:#0f0; }
.status.manual { color:#f33; }
.telemetry { display:grid; grid-template-columns:1fr 1fr; gap:10px; margin-top:15px; }
.tile { background:#222; padding:10px; border-radius:8px; text-align:center; transition:background 0.3s; }
.tile span { font-size:1.2em; font-weight:bold; }
.safe { background:#262; }
.warn { background:#622; }
.controls { margin-top:20px; text-align:center; }
input[type=password] { width:80%; padding:10px; margin-bottom:10px; font-size:1em; }
button { margin:5px; padding:15px; font-size:1.2em; border:none; border-radius:8px; width:45%; }
.btn-on { background:#0a0; color:#fff; }
.btn-off { background:#a00; color:#fff; }
.toggle { width:45%; }
.on { background:#0a0; color:#fff; }
.off { background:#555; color:#fff; }
</style>
</head>
<body>
<h2>Rover Dashboard</h2>
<div id="status" class="status">?</div>

<div class="telemetry">
<div id="tf" class="tile">Front<br><span id="f">?</span> cm</div>
<div id="tfl" class="tile">Front Left<br><span id="fl">?</span> cm</div>
<div id="tfr" class="tile">Front Right<br><span id="fr">?</span> cm</div>
<div id="trl" class="tile">Rear Left<br><span id="rl">?</span> cm</div>
<div id="trr" class="tile">Rear Right<br><span id="rr">?</span> cm</div>
<div class="tile">Temp<br><span id="t">?</span> °C</div>
<div class="tile">Humidity<br><span id="h">?</span> %</div>
<div class="tile">Speed<br><span id="s">?</span> m/s</div>
</div>

<div class="controls">
<h3>Mode Control</h3>
<input type="password" id="pass" placeholder="Password"><br>
<button class="btn-on" onclick="setMode(1)">Enable Auto</button>
<button class="btn-off" onclick="setMode(0)">Disable Auto</button>

<h3>Actuators</h3>
<button id="btnHorn" class="toggle off" onclick="toggle('horn')">Horn</button>
<button id="btnLights" class="toggle off" onclick="toggle('lights')">Lights</button>
</div>

<script>
var ws = new WebSocket("ws://" + location.hostname + ":81/");
ws.onmessage = function(evt) {
  var data = JSON.parse(evt.data);
  var st = document.getElementById("status");
  if(data.mode) { st.textContent="Autonomous"; st.className="status auto"; }
  else { st.textContent="Manual"; st.className="status manual"; }

  // Distances
  updateTile("f","tf",data.dist[0],1);
  updateTile("fl","tfl",data.dist[1],1);
  updateTile("fr","tfr",data.dist[2],1);
  updateTile("rl","trl",data.dist[3],1);
  updateTile("rr","trr",data.dist[4],1);

  document.getElementById("t").textContent  = formatReading(data.temp,1);
  document.getElementById("h").textContent  = formatReading(data.hum,1);
  document.getElementById("s").textContent  = formatReading(data.speed,2);

  // Update horn/lights buttons
  updateButton("btnHorn", data.horn);
  updateButton("btnLights", data.lights);
};
function updateTile(valId,tileId,value,decimals) {
  var display = formatReading(value,decimals);
  document.getElementById(valId).textContent = display;
  var tile = document.getElementById(tileId);
  if(display === "--") {
    tile.className="tile";
  } else {
    var numeric = Number(value);
    if(!Number.isNaN(numeric) && numeric>0 && numeric<100) { tile.className="tile warn"; }
    else { tile.className="tile safe"; }
  }
}
function formatReading(value,decimals) {
  if(value === null || value === undefined) { return "--"; }
  var num = Number(value);
  if(Number.isNaN(num)) { return "--"; }
  if(typeof decimals === "number") { return num.toFixed(decimals); }
  return num.toString();
}
function updateButton(id,state) {
  var btn = document.getElementById(id);
  if(state) { btn.className="toggle on"; }
  else { btn.className="toggle off"; }
}
function setMode(m) {
  var p = document.getElementById("pass").value;
  ws.send(JSON.stringify({cmd:"setmode",mode:m,pass:p}));
}
function toggle(act) {
  ws.send(JSON.stringify({cmd:act}));
}
</script>
</body>
</html>
)rawliteral";

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(115200);
  nano1.begin(115200, SERIAL_8N1, NANO1_RX, NANO1_TX);
  nano2.begin(115200, SERIAL_8N1, NANO2_RX, NANO2_TX);

  WiFi.softAP(ssid, password);
  udp.begin(localPort);

  server.on("/", [](){ server.send(200, "text/html", htmlPage); });
  server.begin();

  webSocket.begin();
  webSocket.onEvent(handleWebSocketEvent);
}

// =========================
// Loop
// =========================
void loop() {
  handleUDP();
  forwardTelemetry();
  if (autonomousMode) {
    autonomousControl();
  }
  checkFailsafe();

  server.handleClient();
  webSocket.loop();
}

// =========================
// WebSocket
// =========================
void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_TEXT) {
    DynamicJsonDocument doc(256);
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      String cmd = doc["cmd"];
      if (cmd == "setmode") {
        if (String(doc["pass"]) == webPass) {
          autonomousMode = doc["mode"] == 1;
        }
      } else if (cmd == "horn") {
        hornOn = !hornOn;
      } else if (cmd == "lights") {
        lightsOn = !lightsOn;
      }
    }
  }
}

void sendStatus() {
  DynamicJsonDocument doc(256);
  doc["mode"] = autonomousMode;
  doc["horn"] = hornOn;
  doc["lights"] = lightsOn;
  JsonArray arr = doc.createNestedArray("dist");
  for (int i=0;i<5;i++) {
    if (isnan(distances[i])) arr.add(nullptr);
    else arr.add(distances[i]);
  }
  if (isnan(temp)) doc["temp"] = nullptr; else doc["temp"] = temp;
  if (isnan(hum)) doc["hum"] = nullptr; else doc["hum"] = hum;
  if (isnan(speed)) doc["speed"] = nullptr; else doc["speed"] = speed;

  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
}

// =========================
// UDP Control
// =========================
void handleUDP() {
  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    char packetBuffer[256] = {0};
    int len = udp.read(packetBuffer, sizeof(packetBuffer) - 1);
    if (len > 0) packetBuffer[len] = '\0';

    remoteIP = udp.remoteIP();
    remotePort = udp.remotePort();
    lastPacketTime = millis();
    failsafeActive = false;

    if (strstr(packetBuffer, "AUTO:1") != NULL) autonomousMode = true;
    else if (strstr(packetBuffer, "AUTO:0") != NULL) autonomousMode = false;

    if (!autonomousMode) {
      nano2.print(packetBuffer);
      nano2.print("\n");
    }
  }
}

// =========================
// Telemetry
// =========================
void forwardTelemetry() {
  while (nano1.available()) {
    char c = nano1.read();
    if (c == '\n') {
      telemetryBuffer[telemetryIndex] = '\0';
      String telemetry = String(telemetryBuffer);
      telemetry.trim();

      if (telemetry.startsWith("S:")) parseTelemetry(telemetry);

      if (remoteIP) {
        udp.beginPacket(remoteIP, remotePort);
        udp.print(telemetry);
        udp.endPacket();
      }

      sendStatus();
      telemetryIndex = 0;
    } else if (c != '\r') {
      if (telemetryIndex < TELEMETRY_BUFFER_SIZE - 1) {
        telemetryBuffer[telemetryIndex++] = c;
      }
    }
  }
}

float parseTelemetryToken(const char* tok) {
  if (!tok) return NAN;
  String token = String(tok);
  token.trim();
  if (token.length() == 0) return NAN;
  token.toLowerCase();
  if (token == "null" || token == "nan") return NAN;
  return token.toFloat();
}

void parseTelemetry(String line) {
  line.remove(0, 2);
  float values[8] = {NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN};
  int idx = 0;
  char buf[128];
  line.toCharArray(buf, sizeof(buf));
  char* context = nullptr;
  char* tok = strtok_r(buf, ",;", &context);
  while (tok && idx < 8) {
    values[idx++] = parseTelemetryToken(tok);
    tok = strtok_r(NULL, ",;", &context);
  }
  for (int i = 0; i < 5; i++) {
    distances[i] = (i < idx) ? values[i] : NAN;
  }
  temp = (idx > 5) ? values[5] : NAN;
  hum = (idx > 6) ? values[6] : NAN;
  speed = (idx > 7) ? values[7] : NAN;
}

bool distanceBlocked(float reading) {
  return !isnan(reading) && reading > 0 && reading < obstacleThreshold;
}

float usableDistance(float reading) {
  if (isnan(reading) || reading <= 0) return 0.0f;
  return reading;
}

int determineCruiseThrottle() {
  if (!isnan(speed)) {
    if (speed < targetSpeed - 0.05f) return 120;
    if (speed > targetSpeed + 0.05f) return 80;
    return 100;
  }
  return 110;
}

// =========================
// Autonomous logic
// =========================
void autonomousControl() {
  if (failsafeActive) {
    return;
  }
  int steer = 90;
  int throttle = determineCruiseThrottle();

  bool frontSensorValid = !isnan(distances[0]);
  bool frontLeftBlocked = distanceBlocked(distances[1]);
  bool frontRightBlocked = distanceBlocked(distances[2]);
  bool frontBlocked = frontSensorValid ? distanceBlocked(distances[0]) : (frontLeftBlocked || frontRightBlocked);
  bool rearLeftBlocked = distanceBlocked(distances[3]);
  bool rearRightBlocked = distanceBlocked(distances[4]);

  if (frontBlocked) {
    if (throttle > 80) throttle = 80;
    if (frontLeftBlocked && !frontRightBlocked) {
      steer = 60;
    } else if (frontRightBlocked && !frontLeftBlocked) {
      steer = 120;
    } else if (frontLeftBlocked && frontRightBlocked) {
      float leftClearance = usableDistance(distances[1]);
      float rearLeftClearance = usableDistance(distances[3]);
      if (rearLeftClearance > leftClearance) leftClearance = rearLeftClearance;
      float rightClearance = usableDistance(distances[2]);
      float rearRightClearance = usableDistance(distances[4]);
      if (rearRightClearance > rightClearance) rightClearance = rearRightClearance;
      steer = (rightClearance > leftClearance) ? 120 : 60;
      if (rearLeftBlocked && rearRightBlocked) {
        throttle = 90; // stop if boxed in
      } else {
        throttle = 70; // gently reverse to create space
      }
    } else {
      steer = 90;
    }
  } else {
    if (frontLeftBlocked && !frontRightBlocked) {
      steer = 60;
      if (throttle > 90) throttle = 90;
    } else if (frontRightBlocked && !frontLeftBlocked) {
      steer = 120;
      if (throttle > 90) throttle = 90;
    } else if (frontLeftBlocked && frontRightBlocked) {
      float leftClearance = usableDistance(distances[1]);
      float rightClearance = usableDistance(distances[2]);
      steer = (rightClearance > leftClearance) ? 120 : 60;
      if (throttle > 90) throttle = 90;
    } else {
      steer = 90;
    }
  }

  String cmd = "STEER:" + String(steer) +
               ";THROT:" + String(throttle) +
               ";HORN:" + String(hornOn ? 1 : 0) +
               ";LIGHTS:" + String(lightsOn ? 1 : 0) +
               ";AUTO:1;";
  nano2.println(cmd);
}

// =========================
// Failsafe
// =========================
void checkFailsafe() {
  if (millis() - lastPacketTime > failsafeTimeout && !failsafeActive) {
    failsafeActive = true;
    nano2.println("CMD FAILSAFE");
    nano2.flush();
  }
}
