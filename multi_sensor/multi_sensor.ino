const int ledPin = 9;
const int sensor1 = A0;
const int sensor2 = A4;
const int sensor3 = A5;

int ledBrightness = 0;
bool fadeDirection = 1;
bool anyonePresent = false;
unsigned long detectionStartTime = 0;
const unsigned long MAX_HOLD_TIME = 2000; // Force reset after 2 seconds (Adjust as needed)

void setup() {
  Serial.begin(9600);      
  pinMode(ledPin, OUTPUT); 
  pinMode(sensor1, INPUT);
  pinMode(sensor2, INPUT);
  pinMode(sensor3, INPUT);
  Serial.println("System Online - Fast Reset Mode");
}

void loop() {
  bool s1 = digitalRead(sensor1);
  bool s2 = digitalRead(sensor2);
  bool s3 = digitalRead(sensor3);

  bool currentDetection = (s1 || s2 || s3);

  // 1. Detection Logic with Software Timeout
  if (currentDetection) {
    if (!anyonePresent) {
      // Just triggered
      anyonePresent = true;
      detectionStartTime = millis();
      Serial.print("TARGET DETECTED: ");
      if (s1) Serial.print("[A0] ");
      if (s2) Serial.print("[A4] ");
      if (s3) Serial.print("[A5] ");
      Serial.println();
    }

    // Check if we've been holding HIGH for too long (The "Force Reset")
    if (millis() - detectionStartTime > MAX_HOLD_TIME) {
      // Optional: uncomment the line below to force the light off even if sensor is still HIGH
      // currentDetection = false; 
    }
  } 
  
  // 2. No Detection Logic
  if (!currentDetection && anyonePresent) {
    anyonePresent = false;
    Serial.println("--- AREA CLEAR: No Presence Detected ---");
    // Reset fade variables
    ledBrightness = 0;
    fadeDirection = 1;
    analogWrite(ledPin, 0);
  }

  // 3. LED Fading Animation (only runs if area isn't clear)
  if (anyonePresent) {
    if (fadeDirection == 1) {
      ledBrightness += 10; // Fast fade
    } else {
      ledBrightness -= 10;
    } 

    if (ledBrightness >= 255) { ledBrightness = 255; fadeDirection = 0; }
    if (ledBrightness <= 0) { ledBrightness = 0; fadeDirection = 1; }
    
    analogWrite(ledPin, ledBrightness);
  }

  delay(15); 
}