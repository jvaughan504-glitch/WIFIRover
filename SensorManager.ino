#include <Arduino.h>

// Simulated sensor values
int front = 100, frontLeft = 95, frontRight = 102;
int left = 85, right = 90;
int temperature = 25, humidity = 50;

unsigned long lastSend = 0;
const unsigned long interval = 500; // ms

void setup() {
  Serial.begin(115200);
}

void loop() {
  if (millis() - lastSend > interval) {
    lastSend = millis();

    // Format: S:front,frontLeft,frontRight,left,right,temp,hum;
    Serial.print("S:");
    Serial.print(front); Serial.print(",");
    Serial.print(frontLeft); Serial.print(",");
    Serial.print(frontRight); Serial.print(",");
    Serial.print(left); Serial.print(",");
    Serial.print(right); Serial.print(",");
    Serial.print(temperature); Serial.print(",");
    Serial.print(humidity);
    Serial.println(";");
  }
}