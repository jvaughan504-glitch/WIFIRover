#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =============================
// OLED Setup
// =============================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// =============================
// Sensor + Distance Setup
// =============================
const byte sensorPin = 2;   // Interrupt pin for hall sensor
volatile unsigned long pulseCount = 0;

unsigned long lastTime = 0;
// Distance per pulse = 46 cm / (2.846 shaft revs * 4 magnets)
float distancePerPulse = 46.0 / (2.846 * 4.0); // ~4.04 cm per pulse
float totalDistance = 0;  // in cm

// =============================
// Rolling Average Buffer
// =============================
#define AVG_SIZE 5
float speedBuffer[AVG_SIZE];
int bufferIndex = 0;
bool bufferFilled = false;

// =============================
// Interrupt Service Routine
// =============================
void countPulse() {
  pulseCount++;
}

// =============================
// Setup
// =============================
void setup() {
  Serial.begin(9600);

  // Setup sensor
  pinMode(sensorPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(sensorPin), countPulse, RISING);

  // Setup display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // I2C addr 0x3C
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Speed & Distance");
  display.display();

  lastTime = millis();
}

// =============================
// Main Loop
// =============================
void loop() {
  unsigned long now = millis();

  if (now - lastTime >= 1000) {  // update every 1 second
    noInterrupts();
    unsigned long count = pulseCount;
    pulseCount = 0;
    interrupts();

    // Distance this interval
    float intervalDistance = count * distancePerPulse; // cm
    totalDistance += intervalDistance;

    // Raw speed (m/s)
    float speed_cm_s = intervalDistance / ((now - lastTime) / 1000.0);
    float rawSpeed_m_s = speed_cm_s / 100.0; // cm/s → m/s

    // Add to rolling buffer
    speedBuffer[bufferIndex] = rawSpeed_m_s;
    bufferIndex++;
    if (bufferIndex >= AVG_SIZE) {
      bufferIndex = 0;
      bufferFilled = true;
    }

    // Compute average
    float avgSpeed_m_s = 0;
    int samples = bufferFilled ? AVG_SIZE : bufferIndex;
    for (int i = 0; i < samples; i++) {
      avgSpeed_m_s += speedBuffer[i];
    }
    if (samples > 0) avgSpeed_m_s /= samples;

    // Debug output to Serial
    Serial.print("Raw Speed: ");
    Serial.print(rawSpeed_m_s, 2);
    Serial.print(" m/s | Avg Speed: ");
    Serial.print(avgSpeed_m_s, 2);
    Serial.print(" m/s | Total Distance: ");
    Serial.print(totalDistance / 100.0, 2); // cm → m
    Serial.println(" m");

    // Update OLED with averaged speed
    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextSize(1);
    display.println("Vehicle Monitor");
    display.println("----------------");
    display.setTextSize(2);
    display.print(avgSpeed_m_s, 2);
    display.println(" m/s");
    display.setTextSize(1);
    display.print("Distance: ");
    display.print(totalDistance / 100.0, 2); // cm → m
    display.println(" m");
    display.display();

    lastTime = now;
  }
}
