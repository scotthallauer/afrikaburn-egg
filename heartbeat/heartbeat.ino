#include <SoftwareSerial.h>
#include <MyLD2410.h>

SoftwareSerial sensorSerial(7, 6);
MyLD2410 sensor(sensorSerial);

// Hardware Constants
const int LED_PIN = 9;

// Config Constants
const int MIN_HEART_RATE = 70;
const int MAX_HEART_RATE = 130;
const int MIN_DISTANCE = 20;
const int MAX_DISTANCE = 400;
const int MIN_OPACITY = 10;
const int MID_OPACITY = 50;
const int MAX_OPACITY = 100;

// State Variables
int heartRate = MIN_HEART_RATE;
long beatStartTime = 0;
int lastDistance = 0;

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  sensorSerial.begin(38400);
  if (sensor.begin()) {
    Serial.println("Sensor online.");
  } else {
    Serial.println("Sensor connection failed.");
  }

  startBeat();
}

void loop() {
  int distance = getObjectDistance();

  int beatProgress = getBeatProgress();
  if (beatProgress >= 100) {
    startBeat();
    heartRate = calculateHeartRate(distance);
  }

  int opacity = getCurrentOpacity(beatProgress);
  updateLight(opacity);

  Serial.print("Distance: "); Serial.print(distance);
  Serial.print(" cm | BPM: "); Serial.print(heartRate);
  Serial.print(" | Opacity: "); Serial.print(opacity);
}

// Returns the detected distance in cm
int getObjectDistance() {
  if (sensor.check() == MyLD2410::DATA && sensor.presenceDetected()) {
    if (sensor.movingTargetDetected()) {
      return sensor.movingTargetDistance();
    } else if (sensor.stationaryTargetDetected()) {
      return sensor.stationaryTargetDistance();
    }
  }
  return lastDistance;
}

// Closer distance = faster heart rate
int calculateHeartRate(int distance) {
  int clampedDistance = constrain(distance, MIN_DISTANCE, MAX_DISTANCE);
  return map(clampedDistance, MIN_DISTANCE, MAX_DISTANCE, MAX_HEART_RATE, MIN_HEART_RATE);
}

void startBeat() {
  beatStartTime = millis();
}

int getBeatProgress() {
  long elapsed = millis() - beatStartTime;
  long beatDuration = 60000 / heartRate;
  int progress = (int)((float)elapsed / beatDuration * 100);
  return min(progress, 100);
}

int getCurrentOpacity(int beatProgress) {
  int opacity;
  if (beatProgress < 15) {
    opacity = map(beatProgress, 0, 15, MIN_OPACITY, MAX_OPACITY);
  } else if (beatProgress < 30) {
    opacity = map(beatProgress, 15, 30, MAX_OPACITY, MIN_OPACITY);
  } else if (beatProgress < 45) {
    opacity = map(beatProgress, 30, 45, MIN_OPACITY, MID_OPACITY);
  } else if (beatProgress < 60) {
    opacity = map(beatProgress, 45, 60, MID_OPACITY, MIN_OPACITY);
  } else {
    opacity = MIN_OPACITY;
  }
  return opacity;
}

void updateLight(int opacity) {
  analogWrite(LED_PIN, getBrightness(opacity));
}

int getBrightness(int opacity) {
  return (int)round(opacity / 100.0 * 255);
}