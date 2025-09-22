diff --git a/SensorManager.ino b/SensorManager.ino
index bbb0b749adf8c6e3db790847ed6524dd828aa349..78999fc9a7a39b00e31652a85c7c9e142b418fb9 100644
--- a/SensorManager.ino
+++ b/SensorManager.ino
@@ -1,30 +1,155 @@
 #include <Arduino.h>
 
-// Simulated sensor values
-int frontLeft = 95, frontRight = 102;
-int left = 85, right = 90;
-int temperature = 25, humidity = 50;
+// =============================
+// Ultrasonic Sensor Definitions
+// =============================
+struct UltrasonicSensor {
+  uint8_t trigPin;
+  uint8_t echoPin;
+};
 
-unsigned long lastSend = 0;
-const unsigned long interval = 500; // ms
+const UltrasonicSensor FRONT_LEFT  = {3, 4};
+const UltrasonicSensor FRONT_RIGHT = {5, 6};
+const UltrasonicSensor REAR_LEFT   = {7, 8};
+const UltrasonicSensor REAR_RIGHT  = {9, 10};
+const UltrasonicSensor sensors[4] = {
+  FRONT_LEFT,
+  FRONT_RIGHT,
+  REAR_LEFT,
+  REAR_RIGHT
+};
 
+const unsigned long ULTRASONIC_TIMEOUT_US = 30000UL; // ~5 m max distance
+const float SOUND_SPEED_CM_PER_US = 0.0343f;
+
+// =============================
+// Speed Sensor Definitions
+// =============================
+const uint8_t SPEED_SENSOR_PIN = 2;
+volatile unsigned long pulseCount = 0;
+
+const float WHEEL_CIRCUMFERENCE_CM = 46.0f;
+const float SHAFT_REV_PER_METER = 2.846f;
+const float MAGNETS_PER_REV = 4.0f;
+const float DISTANCE_PER_PULSE_CM = WHEEL_CIRCUMFERENCE_CM / (SHAFT_REV_PER_METER * MAGNETS_PER_REV);
+
+#define SPEED_AVG_SAMPLES 5
+float speedBuffer[SPEED_AVG_SAMPLES] = {0};
+uint8_t bufferIndex = 0;
+bool bufferFilled = false;
+float totalDistanceCm = 0.0f;
+unsigned long lastSpeedSample = 0;
+
+// =============================
+// Telemetry Timing
+// =============================
+const unsigned long TELEMETRY_INTERVAL_MS = 200;
+unsigned long lastTelemetry = 0;
+
+// =============================
+// Interrupt Service Routine
+// =============================
+void countPulse() {
+  pulseCount++;
+}
+
+// =============================
+// Utility Functions
+// =============================
+float readDistanceCm(const UltrasonicSensor &sensor) {
+  digitalWrite(sensor.trigPin, LOW);
+  delayMicroseconds(2);
+  digitalWrite(sensor.trigPin, HIGH);
+  delayMicroseconds(10);
+  digitalWrite(sensor.trigPin, LOW);
+
+  unsigned long duration = pulseIn(sensor.echoPin, HIGH, ULTRASONIC_TIMEOUT_US);
+  if (duration == 0) {
+    return -1.0f; // timeout
+  }
+
+  return (duration * SOUND_SPEED_CM_PER_US) / 2.0f;
+}
+
+float computeAverageSpeed(float latestSample) {
+  speedBuffer[bufferIndex] = latestSample;
+  bufferIndex++;
+  if (bufferIndex >= SPEED_AVG_SAMPLES) {
+    bufferIndex = 0;
+    bufferFilled = true;
+  }
+
+  float sum = 0.0f;
+  uint8_t samples = bufferFilled ? SPEED_AVG_SAMPLES : bufferIndex;
+  if (samples == 0) {
+    return latestSample;
+  }
+
+  for (uint8_t i = 0; i < samples; ++i) {
+    sum += speedBuffer[i];
+  }
+  return sum / samples;
+}
+
+// =============================
+// Setup & Loop
+// =============================
 void setup() {
   Serial.begin(115200);
+
+  for (const UltrasonicSensor &sensor : sensors) {
+    pinMode(sensor.trigPin, OUTPUT);
+    pinMode(sensor.echoPin, INPUT);
+    digitalWrite(sensor.trigPin, LOW);
+  }
+
+  pinMode(SPEED_SENSOR_PIN, INPUT_PULLUP);
+  attachInterrupt(digitalPinToInterrupt(SPEED_SENSOR_PIN), countPulse, RISING);
+
+  lastTelemetry = millis();
+  lastSpeedSample = lastTelemetry;
 }
 
 void loop() {
-  if (millis() - lastSend > interval) {
-    lastSend = millis();
-
-    // Format: S:front,frontLeft,frontRight,left,right,temp,hum;
-    Serial.print("S:");
-    Serial.print(front); Serial.print(",");
-    Serial.print(frontLeft); Serial.print(",");
-    Serial.print(frontRight); Serial.print(",");
-    Serial.print(left); Serial.print(",");
-    Serial.print(right); Serial.print(",");
-    Serial.print(temperature); Serial.print(",");
-    Serial.print(humidity);
-    Serial.println(";");
+  unsigned long now = millis();
+  if (now - lastTelemetry < TELEMETRY_INTERVAL_MS) {
+    return;
+  }
+  lastTelemetry = now;
+
+  float distances[4];
+  for (uint8_t i = 0; i < 4; ++i) {
+    distances[i] = readDistanceCm(sensors[i]);
+    delay(10); // reduce ultrasonic crosstalk
+  }
+
+  noInterrupts();
+  unsigned long pulses = pulseCount;
+  pulseCount = 0;
+  interrupts();
+
+  float deltaTime = (now - lastSpeedSample) / 1000.0f;
+  if (deltaTime <= 0.0f) {
+    deltaTime = TELEMETRY_INTERVAL_MS / 1000.0f;
+  }
+  lastSpeedSample = now;
+
+  float intervalDistanceCm = pulses * DISTANCE_PER_PULSE_CM;
+  totalDistanceCm += intervalDistanceCm;
+
+  float rawSpeedMps = 0.0f;
+  if (deltaTime > 0.0f) {
+    rawSpeedMps = (intervalDistanceCm / 100.0f) / deltaTime;
+  }
+  float avgSpeedMps = computeAverageSpeed(rawSpeedMps);
+
+  Serial.print("S:");
+  for (uint8_t i = 0; i < 4; ++i) {
+    Serial.print(distances[i], 1);
+    Serial.print(",");
   }
+  Serial.print(avgSpeedMps, 2);
+  Serial.print(",");
+  Serial.print(totalDistanceCm / 100.0f, 2);
+  Serial.println(";");
 }
