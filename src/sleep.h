#pragma once

#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include "can.h"
#include "ble.h"

// Deep sleep while the CAN bus is quiet. The MCP2517FD is put in its own Sleep mode with
// bus wake-up enabled, and its INT pin (an RTC-capable GPIO on the ESP32-C3) wakes the ESP32,
// which then boots and reinitializes everything.
namespace Sleep
{
    const unsigned long INACTIVITY_TIMEOUT = 5000;

    unsigned long last_busy = 0;

    void setup()
    {
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        if (cause == ESP_SLEEP_WAKEUP_GPIO)
        {
            printf("Woke up on CAN bus activity\n");
        }
    }

    // Anything that must not be interrupted by sleep
    bool busy()
    {
        return Ble::pairing_mode || Ble::press_started || Ble::upgrading || Ble::reboot_scheduled;
    }

    void sleep()
    {
        printf("CAN bus inactive for %lu ms, going to sleep\n", millis() - Can::last_activity);
        digitalWrite(LED_BUILTIN, 1);

        if (!Can::sleep())
        {
            printf("Could not put CAN controller to sleep, restarting\n");
            Serial.flush();
            ESP.restart();
        }

#if !SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP
        gpio_deep_sleep_hold_en();
#endif
        esp_deep_sleep_enable_gpio_wakeup(1ULL << Can::MCP2517_INT, ESP_GPIO_WAKEUP_GPIO_LOW);
        Serial.flush();
        esp_deep_sleep_start();
    }

    void loop()
    {
        unsigned long now = millis();
        if (busy())
        {
            last_busy = now;
            return;
        }
        if (now - Can::last_activity > INACTIVITY_TIMEOUT && now - last_busy > INACTIVITY_TIMEOUT)
        {
            sleep();
        }
    }
}
