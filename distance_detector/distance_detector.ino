#include <SoftwareSerial.h>
#include <MyLD2410.h>

SoftwareSerial sensorSerial(10, 11);
MyLD2410 sensor(sensorSerial);

const int sensorPin = A0;   // The LD2410C OUT pin
const int ledPin = 9;       // Your LED connected to PWM pin D9

int ledBrightness = 0;

void setup() {
  // 115200 is generally better for the Serial Monitor when printing lots of data
  Serial.begin(115200);      
  pinMode(ledPin, OUTPUT); 
  
  // Start the serial stream to the sensor (ensure sensor is set to 38400 via the app)
  sensorSerial.begin(38400); 
  sensor.begin();

  Serial.println("Serial radar system online. Waiting for targets...");
}

void loop() {
  // if (sensor.check() == MyLD2410::DATA) {
  //  if (sensor.movingTargetDetected()) {
  //     int movingDist = sensor.movingTargetDistance();
  //     Serial.println(movingDist);
  //   }
  // }
  // int sensorState = digitalRead(sensorPin); 

  // if (sensorState == HIGH && ledBrightness < 255) {
  //  analogWrite(ledPin, ledBrightness++);
  // } else if (sensorState == LOW && ledBrightness > 0) {
  //   analogWrite(ledPin, ledBrightness--);
  // }

  delay(10); 
}