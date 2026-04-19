const int sensorPin = A0;   // The LD2410C OUT pin
const int ledPin = 9;       // Your LED connected to PWM pin D9

// We use this to remember if the LED is already on
bool isPersonPresent = false; 

void setup() {
  Serial.begin(9600);      
  pinMode(sensorPin, INPUT); 
  pinMode(ledPin, OUTPUT); 
  Serial.println("Fade-in radar system online.");
}

void loop() {
  int sensorState = digitalRead(sensorPin); 

  // If someone is detected AND the LED isn't already on...
  if (sensorState == HIGH && isPersonPresent == false) {
    Serial.println("Person detected! Fading in...");
    
    // Loop from 0 (off) to 255 (max brightness)
    for (int brightness = 0; brightness <= 255; brightness++) {
      analogWrite(ledPin, brightness); // Set the brightness level
      delay(5);                        // Wait 5 milliseconds between each step
    }
    // 255 steps * 5ms = about 1.2 seconds to reach full brightness
    
    isPersonPresent = true; // Remember that the LED is now fully ON
  } 
  
  // If no one is detected AND the LED is currently on...
  else if (sensorState == LOW && isPersonPresent == true) {
    Serial.println("Room empty. Turning off instantly.");
    
    analogWrite(ledPin, 0);  // Turn the LED completely OFF
    isPersonPresent = false; // Remember that the LED is now OFF
  }

  // A tiny 50ms delay just to keep the loop from running way too fast
  delay(50); 
}