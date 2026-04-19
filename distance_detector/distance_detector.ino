#include <SoftwareSerial.h>
#include <MyLD2410.h>

// --- Sensor Setup ---
// RX on Pin 10, TX on Pin 11 (Remember to use a voltage divider on Pin 11!)
SoftwareSerial sensorSerial(10, 11); 
MyLD2410 sensor(sensorSerial);


const int ledPin = 9;         // Your LED connected to PWM pin D9
bool isPersonPresent = false; 

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
  // Read the serial buffer. Only execute the logic if a complete data frame arrives.
  if (sensor.check() == MyLD2410::DATA) {
    
    // Grab the T/F 
    bool sensorState = sensor.presenceDetected(); 

    
    // If someone is detected AND the LED isn't already on
    if (sensorState == true && isPersonPresent == false) {
      Serial.print("Person detected! Fading in... ");
      
      // Lets grab the distance since we now have access to it
      if (sensor.movingTargetDetected()) {
        Serial.print("(Moving target at ");
        Serial.print(sensor.movingTargetDistance());
        Serial.println(" cm)");
      } else if (sensor.stationaryTargetDetected()) {
        Serial.print("(Stationary target at ");
        Serial.print(sensor.stationaryTargetDistance());
        Serial.println(" cm)");
      }
      
      // Loop from 0 to 255 
      for (int brightness = 0; brightness <= 255; brightness++) {
        analogWrite(ledPin, brightness); 
        delay(10); // 255 steps * 10ms = about 2.5 seconds to reach full brightness
      }
      
      isPersonPresent = true; // Remember that the LED is now fully ON
    } 
    
    // If no one is detected AND the LED is currently on...
    else if (sensorState == false && isPersonPresent == true) {
      Serial.println("Room empty. Turning off instantly.");
      
      analogWrite(ledPin, 0);  // Turn the LED completely OFF
      isPersonPresent = false; // Remember that the LED is now OFF
    }
  }
  
  // Note: We don't need the 50ms delay at the end of the loop anymore. 
  // sensor.check() naturally paces the loop because it only evaluates to MyLD2410::DATA 
  // when a fresh frame arrives from the sensor (roughly 10-20 times per second).
}