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

int obstacleThreshold = 100;
int targetSpeed = 50;

int distances[5] = {0};
int temp = 0;
int hum = 0;
int speed = 0;

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
<div class="tile">Speed<br><span id="s">?</span> %</div>
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
  updateTile("f","tf",data.dist[0]);
  updateTile("fl","tfl",data.dist[1]);
  updateTile("fr","tfr",data.dist[2]);
  updateTile("rl","trl",data.dist[3]);
  updateTile("rr","trr",data.dist[4]);

  document.getElementById("t").textContent  = data.temp;
  document.getElementById("h").textContent  = data.hum;
  document.getElementById("s").textContent  = data.speed;

  // Update horn/lights buttons
  updateButton("btnHorn", data.horn);
  updateButton("btnLights", data.lights);
};
function updateTile(valId,tileId,value) {
  document.getElementById(valId).textContent = value;
  var tile = document.getElementById(tileId);
  if(value>0 && value<100) { tile.className="tile warn"; }
  else { tile.className="tile safe"; }
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
  if (autonomousMode) autonomousControl();
  else checkFailsafe();

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
  for (int i=0;i<5;i++) arr.add(distances[i]);
  doc["temp"] = temp;
  doc["hum"] = hum;
  doc["speed"] = speed;

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

void parseTelemetry(String line) {
  line.remove(0, 2);
  int parts[8];
  int idx = 0;
  char buf[128];
  line.toCharArray(buf, sizeof(buf));
  char* tok = strtok(buf, ",;");
  while (tok && idx < 8) {
    parts[idx++] = atoi(tok);
    tok = strtok(NULL, ",;");
  }
  if (idx >= 7) {
    for (int i = 0; i<5; i++) distances[i] = parts[i];
    temp = parts[5];
    hum = parts[6];
    if (idx >= 8) speed = parts[7];
  }
}

// =========================
// Autonomous logic
// =========================
void autonomousControl() {
  int steer = 90, throttle = 90;
  bool frontBlocked = (distances[0] > 0 && distances[0] < obstacleThreshold);
  bool leftBlocked  = (distances[1] > 0 && distances[1] < obstacleThreshold);
  bool rightBlocked = (distances[2] > 0 && distances[2] < obstacleThreshold);

  if (frontBlocked && leftBlocked && rightBlocked) {
    int rearLeft = distances[3];
    int rearRight = distances[4];
    throttle = 70;
    steer = (rearLeft > rearRight) ? 60 : 120;
  } else {
    if (!frontBlocked) steer = 90;
    else if (!leftBlocked && rightBlocked) steer = 60;
    else if (!rightBlocked && leftBlocked) steer = 120;
    else steer = (distances[1] > distances[2]) ? 60 : 120;

    if (speed < targetSpeed) throttle = 120;
    else if (speed > targetSpeed) throttle = 80;
    else throttle = 100;
  }

  String cmd = "STEER:" + String(steer) + ";THROT:" + String(throttle) + ";HORN:" + String(hornOn ? 1:0) + ";LIGHTS:" + String(lightsOn ? 1:0) + ";AUTO:1;";
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
