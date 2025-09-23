//VehicleManager
#include <Arduino.h>
#include <Servo.h>

// === Serial ===
#define SERIAL_ESP32 Serial
#define BAUD 115200

// === Servos ===
#define FRONT_SERVO_PIN 10
#define REAR_SERVO_PIN 11
Servo frontServo;
Servo rearServo;

// === ESC ===
#define ESC_PIN 12
Servo esc;

// === Horn & Lights ===
#define HORN_PIN 13
#define LIGHTS_PIN 9

// === Control variables ===
int steering = 90;
int throttle = 90;
bool hornOn = false;
bool lightsOn = false;
bool autonomousMode = false;

// === Watchdog ===
unsigned long lastCommandTime = 0;
const unsigned long TIMEOUT = 500; // ms

// === Serial buffer ===
#define BUF_SIZE 128
char serialBuffer[BUF_SIZE];
int bufPos = 0;

void setup() {
  SERIAL_ESP32.begin(BAUD);
  frontServo.attach(FRONT_SERVO_PIN);
  rearServo.attach(REAR_SERVO_PIN);
  esc.attach(ESC_PIN);
  
  pinMode(HORN_PIN, OUTPUT);
  pinMode(LIGHTS_PIN, OUTPUT);

  // Initialize outputs
  frontServo.write(90);
  rearServo.write(90);
  esc.write(90);
  digitalWrite(HORN_PIN, LOW);
  digitalWrite(LIGHTS_PIN, LOW);
}

void loop() {
  receiveCommands();
  checkFailsafe();
  applyControls();
}

// ----------------------
// Receive commands
// ----------------------
void receiveCommands() {
  while (SERIAL_ESP32.available()) {
    char c = SERIAL_ESP32.read();

    if (c == '\n') {
      serialBuffer[bufPos] = '\0'; 
      parseCommands(serialBuffer);
      bufPos = 0; 
    } else if (bufPos < BUF_SIZE - 1) {
      serialBuffer[bufPos++] = c;
    }
  }
  lastCommandTime = millis();
}

// ----------------------
// Parse incoming command
// ----------------------
void parseCommands(char* packet){
  char* token = strtok(packet,";");
  while(token != NULL){
    if(strncmp(token,"STEER:",6)==0) steering = atoi(token+6);
    else if(strncmp(token,"THROT:",6)==0) throttle = atoi(token+6);
    else if(strncmp(token,"HORN:",5)==0) hornOn = atoi(token+5);
    else if(strncmp(token,"LIGHTS:",7)==0) lightsOn = atoi(token+7);
    else if(strncmp(token,"AUTO:",5)==0) autonomousMode = atoi(token+5);
    token = strtok(NULL,";");
  }
}

// ----------------------
// Check failsafe
// ----------------------
void checkFailsafe() {
  if(millis() - lastCommandTime > TIMEOUT){
    steering = 90;
    throttle = 90;
    hornOn = false;
    lightsOn = false;
    autonomousMode = false;
  }
}

// ----------------------
// Apply controls
// ----------------------
void applyControls() {
  frontServo.write(steering);
  rearServo.write(steering);
  esc.write(throttle);
  digitalWrite(HORN_PIN, hornOn ? HIGH : LOW);
  digitalWrite(LIGHTS_PIN, lightsOn ? HIGH : LOW);
}
