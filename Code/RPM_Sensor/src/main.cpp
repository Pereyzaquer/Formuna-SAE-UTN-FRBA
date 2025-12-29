#include <Arduino.h>

void setup() {
  // put your setup code here, to run once:
  pinMode(8, OUTPUT);
}

void loop() {
  // put your main code here, to run repeatedly:
  delay(1000);
  digitalWrite(8,HIGH);
  delay(1000);
  digitalWrite(8,LOW);
}
