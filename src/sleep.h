#pragma once

#include <Arduino.h>
#include <esp_sleep.h>

namespace Sleep {
unsigned long wake_time = 1*60*1000;
unsigned long latest_action = -wake_time;

void loop() {
    if (millis() - latest_action > wake_time) {
        esp_sleep_enable_timer_wakeup(5000000);
        esp_light_sleep_start();
        delay(5000);
    }
}

void seenAction() {
    latest_action = millis();
}
}