const int ledPin = 9;       // Your LED connected to PWM pin D9

int ledBrightness = 0;
bool fadeDirection = 1;

void setup() {
  Serial.begin(9600);      
  pinMode(ledPin, OUTPUT); 
  Serial.println("System Online");
}

void loop() {
  if (fadeDirection == 1) {
    analogWrite(ledPin, ledBrightness++);
  } else {
    analogWrite(ledPin, ledBrightness--);
  } 

  if (ledBrightness >= 255) {
    fadeDirection = 0;
    Serial.println("Fading Out");
  } else if (ledBrightness <= 0) {
    fadeDirection = 1;
    Serial.println("Fading In");
  }

  delay(10); 
}