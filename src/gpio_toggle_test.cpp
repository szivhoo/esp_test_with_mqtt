#include <Arduino.h>
#include "gpio_toggle_test.h"

#define LED_PIN 23   // most common ESP32 LED pin

void app_init() {
  pinMode(LED_PIN, OUTPUT);
}

void app_loop() {
  digitalWrite(LED_PIN, HIGH);
  delay(3000);
  digitalWrite(LED_PIN, LOW);
  delay(3000);
}