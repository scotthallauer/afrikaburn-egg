const int sensorPin = A0;   // The LD2410C OUT pin
const int ledPin = 9;       // Your LED connected to PWM pin D9

int ledBrightness = 0;

void setup() {
  Serial.begin(9600);      
  pinMode(sensorPin, INPUT); 
  pinMode(ledPin, OUTPUT); 
  Serial.println("System Online");
}

void loop() {
  int sensorState = digitalRead(sensorPin); 

  if (sensorState == HIGH && ledBrightness < 255) {
    analogWrite(ledPin, ledBrightness++);
    Serial.println("Fading In (" + String(ledBrightness) + "/255)");
  } else if (sensorState == LOW && ledBrightness > 0) {
    analogWrite(ledPin, ledBrightness--);
    Serial.println("Fading Out (" + String(ledBrightness) + "/255)");
  }

  delay(10); 
}