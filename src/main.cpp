

// #include <NimBLEDevice.h>
// #include <NimBLEOta.h>
// #include "data.h"
#include "can.h"
#include "pwm.h"
#include "ble.h"
// #include "sleep.h"
#include <Preferences.h>

Preferences prefs;

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(9600);
  delay(500);
  printf("Fahrenheat 2.0\n");

  Pwm::setup();
  Can::setup();
  Ble::setup();
  Ble::setRequestHeatingCallback(Can::setSending);
  Can::setUpdateDataCallback(Ble::updateData);
  Can::setHeatingRequestStateCallback(Ble::updateHeatingState);
}

void loop() {
  Can::loop();
  Ble::loop();
  // Sleep::loop();
}
