#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEOta.h>
#include <Preferences.h>
#include <esp_sleep.h>

namespace Ble
{
    const std::string DEV_NAME = "Fahrenheat";
    const unsigned long PAIR_DELAY = 1000;
    const unsigned long PAIR_TIME = 30000;
    const unsigned long RESET_DELAY = 8000;
    bool pairing_mode = false;
    unsigned long pairing_mode_start = 0;

    bool press_started = false;
    unsigned long press_start = 0;
    bool upgrading = false;

    Preferences prefs;
    static NimBLEOta bleOta;
    NimBLECharacteristic *pDataCharacteristic = nullptr;
    NimBLECharacteristic *pControlCharacteristic = nullptr;

    void (*requestHeatingCallback)(bool) = nullptr;

    void updateData(uint8_t *data, size_t size) {
        if (upgrading) return;
        if (pDataCharacteristic != nullptr) {
            pDataCharacteristic->setValue(data, size);
            pDataCharacteristic->indicate();
        }
    }

    void setRequestHeatingCallback(void (*func)(bool)) {
        requestHeatingCallback = func;
    }

    void updateHeatingState(bool new_state) {
        if (upgrading) return;
        pControlCharacteristic->setValue(new_state);
        pControlCharacteristic->indicate();
        printf("Notifying! %d\n", new_state);
    }

    void stopPairing()
    {
        pairing_mode = false;
        NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
        pAdvertising->stop();
        pAdvertising->setScanFilter(true, true);
        pAdvertising->start();
        digitalWrite(LED_BUILTIN, 1);
    }

    void startPairing()
    {
        pairing_mode_start = millis();
        pairing_mode = true;
        NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
        pAdvertising->stop();
        pAdvertising->setScanFilter(false, false);
        pAdvertising->start();
    }

    bool recentlyConnected = false;
    unsigned long recentlyConnectedTime = 0;

    class ServerCallbacks : public NimBLEServerCallbacks
    {
        void onAuthenticationComplete(NimBLEConnInfo &connInfo) override
        {
            /** Check that encryption was successful, if not we disconnect the client */
            if (!connInfo.isEncrypted())
            {
                NimBLEDevice::getServer()->disconnect(connInfo.getConnHandle());
                Serial.printf("Encrypt connection failed - disconnecting client\n");
                return;
            }
            BLEDevice::whiteListAdd(connInfo.getIdAddress());
            stopPairing();
            Serial.printf("Secured connection to: %s\n", connInfo.getAddress().toString().c_str());
        }
    } serverCallbacks;

    class NameCharacteristicCallbacks : public NimBLECharacteristicCallbacks
    {
        void onRead(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override
        {
        }
        void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override
        {
            NimBLEAttValue val = pCharacteristic->getValue();
            NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
            const uint8_t *name = val.data();

            prefs.putString("name", String(val.data(), val.length()));

            uint16_t length = val.length();
            uint8_t data[length + 2] = {0};
            memcpy(data, &length, 2);
            memcpy(&data[2], name, length);
            pAdvertising->clearData();
            pAdvertising->addServiceUUID("ABCD");
            pAdvertising->setName(DEV_NAME);
            pAdvertising->setManufacturerData(data, sizeof(data));
            pAdvertising->refreshAdvertisingData();
        }
    } nameCallbacks;

    class ControlCharacteristicCallbacks : public NimBLECharacteristicCallbacks
    {
        void onRead(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override
        {
        }
        void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override
        {
            NimBLEAttValue val = pCharacteristic->getValue();
            if (val.length() != 1) return;
            uint8_t data = val.data()[0];
            if (requestHeatingCallback != nullptr) {
                requestHeatingCallback(data == 1);
            }
            upgrading = data == 2;
        }
    } controlCallbacks;

    bool reboot_scheduled = false;
    unsigned long reboot_scheduled_time = 0;

    class OtaCallbacks : public NimBLEOtaCallbacks
    {
        void onComplete(NimBLEOta *ota) override
        {
            reboot_scheduled = true;
            reboot_scheduled_time = millis();
        }
    } otaCallbacks;

    void setup()
    {
        Serial.begin(115200);
        Serial.setDebugOutput(true);
        delay(200);
        pinMode(LED_BUILTIN, OUTPUT);
        pinMode(9, INPUT_PULLUP);
        digitalWrite(LED_BUILTIN, 1);
        Serial.println("Starting NimBLE Server");
        prefs.begin("fahrenheat", false);
        NimBLEDevice::init(DEV_NAME);

        for (size_t i = 0; i < NimBLEDevice::getNumBonds(); i++)
        {
            const auto &addr = NimBLEDevice::getBondedAddress(i);
            NimBLEDevice::whiteListAdd(addr);
        }
        Serial.printf("Restored %d whitelist items from %d bonds\n", NimBLEDevice::getWhiteListCount(), NimBLEDevice::getNumBonds());
        NimBLEDevice::setPower(3); /** +3db */

        NimBLEDevice::setSecurityAuth(true, true, true); /** bonding, MITM, BLE secure connections */
        NimBLEDevice::setSecurityPasskey(123456);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO); /** Display only passkey */

        NimBLEServer *pServer = NimBLEDevice::createServer();
        bleOta.start(&otaCallbacks, true);
        pServer->setCallbacks(&serverCallbacks);
        NimBLEService *pService = pServer->createService("ABCD");
        // NimBLECharacteristic *pNonSecureCharacteristic = pService->createCharacteristic("1234", NIMBLE_PROPERTY::READ);
        pDataCharacteristic =
            pService->createCharacteristic("BABE",
                                           NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
        pControlCharacteristic =
            pService->createCharacteristic("DEAD",
                                           NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::NOTIFY);
        NimBLECharacteristic *pNameCharacteristic =
            pService->createCharacteristic("F00D",
                                           NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC);

        pNameCharacteristic->setCallbacks(&nameCallbacks);
        pControlCharacteristic->setCallbacks(&controlCallbacks);

        String devName = prefs.getString("name", "MyDevice");
        pNameCharacteristic->setValue(devName);
        printf("Name is %s\n", devName);
        pControlCharacteristic->setValue(0);
        pService->start();

        NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
        pAdvertising->addServiceUUID("ABCD");
        pAdvertising->addServiceUUID("8018");

        uint16_t length = devName.length();
        uint8_t data[length + 2] = {0};
        memcpy(data, &length, 2);
        memcpy(&data[2], devName.c_str(), length);
        pAdvertising->setManufacturerData(data, sizeof(data));
        pAdvertising->setName(DEV_NAME);
        pAdvertising->setScanFilter(true, true);
        pAdvertising->start();
    }

    bool sleep = false;
    unsigned long last_sleep_wake = 0;
    unsigned long last_wake = 0;

    void loop()
    {
        NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
        bool button_down = digitalRead(9) == 0;

        if (!press_started && button_down) // Button down event
        {
            press_started = true;
            press_start = millis();
        }
        else if (press_started && !button_down) // Button up event
        {
            // Release
            press_started = false;
            unsigned long duration = millis() - press_start;

            if (duration > RESET_DELAY)
            {
                printf("Clearing and rebooting\n");
                BLEDevice::deleteAllBonds();
                delay(50);
                ESP.restart();
            }
            else if (duration < PAIR_DELAY)
            {
                Serial.printf("444Restored %d whitelist items from %d bonds. %d connected.\n", NimBLEDevice::getWhiteListCount(), NimBLEDevice::getNumBonds(), NimBLEDevice::getServer()->getConnectedCount());
            }
        }
        else if (press_started && button_down)
        { // Button held event
            unsigned long duration = millis() - press_start;
            if (duration > RESET_DELAY)
            {
                stopPairing();
                digitalWrite(LED_BUILTIN, 0);
            }
            else if (duration > PAIR_DELAY && !pairing_mode)
            {
                startPairing();
            }
        }

        if (pairing_mode) // Disable pairing after some time
        {
            unsigned long duration = millis() - pairing_mode_start;
            digitalWrite(LED_BUILTIN, ((duration >> 8) & 1) == 0);
            if (duration > PAIR_TIME)
            {
                stopPairing();
            }
        }

        if (reboot_scheduled)
        {
            unsigned long duration = millis() - reboot_scheduled_time;
            if (duration > 2000)
            {
                esp_restart();
            }
        }

        if (!pAdvertising->isAdvertising()) // Keep advertisement alive
        {
            pAdvertising->start();
        }


        // if ((millis() - last_sleep_wake) > 500) {
        //     printf("Going to sleep %d\n", millis());
        //     pAdvertising->stop();
        //     if (NimBLEDevice::getServer()->getConnectedCount() == 0) {
        //         printf("Sleeping for real\n");
        //         esp_sleep_enable_timer_wakeup(5000000);
        //         esp_light_sleep_start();
        //     }
        //     // sleep = !sleep;
        //     last_sleep_wake = millis();
        // }
        //  else if (!sleep && (millis() - last_sleep_wake) > 4500) {
        //     printf("Woke %d\n", millis());
        //     pAdvertising->start();
        //     sleep = !sleep;
        //     last_sleep_wake= millis();
        // }
    }
}
