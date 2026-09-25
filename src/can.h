#pragma once

#include <ACAN2517FD.h>
#include <SPI.h>
#include <driver/gpio.h>

namespace Can
{

static const unsigned long freshness_interval = 5000;
static const unsigned long max_heating_time = 3 * 60 * 60 * 1000;

// Hardware
static const byte MCP2517_INT = 3; // INT output of MCP2517FD
ACAN2517FD can(SS, SPI, MCP2517_INT);

// MCP2517FD/MCP2518FD registers (DS20005688B) used for sleep handling, not exposed by ACAN2517FD.
// Bit positions match the Linux mcp251xfd driver (drivers/net/can/spi/mcp251xfd/mcp251xfd.h).
static const uint16_t REG_CON = 0x000;
static const uint16_t REG_INT = 0x01C;
static const uint16_t REG_OSC = 0xE00;
static const uint16_t REG_IOCON = 0xE04;
static const uint8_t MODE_SLEEP = 0x01;
static const uint8_t MODE_CONFIGURATION = 0x04;
static const SPISettings rawSpiSettings(800UL * 1000, MSBFIRST, SPI_MODE0);

// State
unsigned long latest_loop_run = 0;
unsigned long last_activity = 0; // millis() of the last received CAN frame
bool do_send = false;
unsigned long do_send_updated_at = 0;
bool first = true;
void (*updateDataCallback)(uint8_t *, size_t) = nullptr;
void (*heatingRequestStateCallback)(bool) = nullptr;

unsigned long bms_info_last_update = -freshness_interval;

// DATA
struct __attribute__((packed)) BatteryData
{
    uint8_t data_version = 1;
    uint32_t data_age = 0xffffffff;

    // bmsInfoCallback
    uint8_t bms_mode = 0; // 0 Invalid, 1 HV_ACTIVE, 2 BALANCING, 3 EXTERN CHARGING, 4 AC_CHARGING, 5 Error, 6 DC_CHARGING, 7 Init
    uint16_t bms_current = 0;
    uint16_t bms_voltage = 0;

    // bmsInfo2Callback
    uint16_t bms_pack_voltage = 0;

    // dynamicDischargeCallback
    uint16_t max_charge_power_watt = 0;  // X * 100
    uint16_t max_charge_current_amp = 0; // X * 0.2

    // batteryStatusCallback
    uint16_t battery_SOC = 0;             // X * 0.05
    uint16_t usable_energy_amount_Wh = 0; // X * 5

    // thermalCallback
    uint8_t coolant_temperature = 0;

    // chargingOptimizationCallback
    uint8_t temperature_status_charge = 0; // 0 init, 1 temp under optimal, 2 temp optimal, 3 temp over optimal, 7 fault

    // bmsSafetyCallback
    uint8_t battery_min_temp = 0;    // X * 0.5 - 40
    uint8_t battery_max_temp = 0;    // X * 0.5 - 40
    uint16_t cellvoltage_highest_mV; // X + 1000
    uint16_t cellvoltage_lowest_mV;  // X + 1000

    // heatingStatusCallback
    uint8_t battery_heating_active = 0; // bool
    uint8_t power_battery_heating_watt = 0;
    uint8_t power_battery_heating_req_watt = 0;
} data;

// diagnosticCallback
bool session_error = false;

void bmsInfoCallback(const CANFDMessage &frame)
{
    bms_info_last_update = millis();
    data.bms_mode = (frame.data[2] & 0x07);
    data.bms_current = ((frame.data[4] & 0x7F) << 8) | frame.data[3];
    data.bms_voltage = ((frame.data[7] << 4) + ((frame.data[6] & 0xF0) >> 4));
}

void bmsInfo2Callback(const CANFDMessage &frame)
{
    data.bms_pack_voltage = ((frame.data[1] & 0x3F) << 8) | frame.data[0];
}

void dynamicDischargeCallback(const CANFDMessage &frame)
{
    data.max_charge_power_watt = ((frame.data[7] << 5) | (frame.data[6] >> 3));           // * 100
    data.max_charge_current_amp = (((frame.data[4] & 0x3F) << 7) | (frame.data[3] >> 1)); // * 0.2
}

void batteryStatusCallback(const CANFDMessage &frame)
{
    if (frame.data[6] != 0xFE || frame.data[7] != 0xFF)
    {                                                                            // Init state, values below invalid
        data.battery_SOC = ((frame.data[3] & 0x0F) << 7) | (frame.data[2] >> 1); //*0.05
        data.usable_energy_amount_Wh = (frame.data[7] << 8) | frame.data[6];     //*5
    }
}

void thermalCallback(const CANFDMessage &frame)
{
    data.coolant_temperature = frame.data[7];
}

void chargingOptimizationCallback(const CANFDMessage &frame)
{
    // 0 init, 1 temp under optimal, 2 temp optimal, 3 temp over optimal, 7 fault
    data.temperature_status_charge = (((frame.data[2] & 0x03) << 1) | frame.data[1] >> 7);
}

void bmsSafetyCallback(const CANFDMessage &frame)
{
    data.battery_max_temp = frame.data[3]; // * 0.5 - 40
    data.battery_min_temp = frame.data[4]; // * 0.5 - 40
    data.cellvoltage_highest_mV = (((frame.data[6] & 0x0F) << 8) | frame.data[5]);
    data.cellvoltage_lowest_mV = ((frame.data[7] << 4) | frame.data[6] >> 4);
}

void diagnosticCallback(const CANFDMessage &frame)
{
    if (frame.data[0] == 0x03 &&
        frame.data[1] == 0x7F &&
        frame.data[2] == 0x2F &&
        frame.data[3] == 0x7F)
    {
        session_error = true;
    }
}

void heatingStatusCallback(const CANFDMessage &frame)
{
    data.battery_heating_active = (frame.data[4] & 0x40) >> 6;
    data.power_battery_heating_watt = frame.data[6];
    data.power_battery_heating_req_watt = frame.data[7];
}

void updateData()
{
    if (updateDataCallback != nullptr)
    {
        updateDataCallback((uint8_t *)&data, sizeof(data));
    }
}

void setUpdateDataCallback(void (*cb)(uint8_t *, size_t))
{
    updateDataCallback = cb;
}

void setHeatingRequestStateCallback(void (*func)(bool))
{
    heatingRequestStateCallback = func;
}

void rawWriteRegister8(uint16_t address, uint8_t value)
{
    SPI.beginTransaction(rawSpiSettings);
    digitalWrite(SS, LOW);
    SPI.transfer16(0x2000 | (address & 0x0FFF)); // Write instruction
    SPI.transfer(value);
    digitalWrite(SS, HIGH);
    SPI.endTransaction();
}

uint8_t rawReadRegister8(uint16_t address)
{
    SPI.beginTransaction(rawSpiSettings);
    digitalWrite(SS, LOW);
    SPI.transfer16(0x3000 | (address & 0x0FFF)); // Read instruction
    uint8_t value = SPI.transfer(0x00);
    digitalWrite(SS, HIGH);
    SPI.endTransaction();
    return value;
}

uint8_t currentMode()
{
    return (rawReadRegister8(REG_CON + 2) >> 5) & 0x07; // CiCON.OPMOD
}

bool waitForMode(uint8_t mode, unsigned long timeout_ms)
{
    unsigned long start = millis();
    while (currentMode() != mode)
    {
        if (millis() - start > timeout_ms)
        {
            return false;
        }
    }
    return true;
}

// Bring the MCP2517FD out of Sleep mode (if it is there) so that begin() can reset it
void wakeController()
{
    uint8_t osc = rawReadRegister8(REG_OSC);
    if ((osc & (1 << 2)) == 0) // OSC.OSCDIS
    {
        return;
    }
    rawWriteRegister8(REG_OSC, osc & ~(1 << 2));
    unsigned long start = millis();
    while ((rawReadRegister8(REG_OSC + 1) & (1 << 2)) == 0) // OSC.OSCRDY
    {
        if (millis() - start > 10)
        {
            printf("MCP2517FD oscillator not ready after wake\n");
            return;
        }
    }
    printf("MCP2517FD woken from sleep\n");
}

// Put the MCP2517FD into Sleep mode with only the bus wake-up interrupt enabled, so that
// its INT pin is pulled low on the next CAN bus activity. Holds CS high while the ESP32 sleeps.
bool sleep()
{
    detachInterrupt(digitalPinToInterrupt(MCP2517_INT));

    rawWriteRegister8(REG_CON + 3, MODE_CONFIGURATION | (1 << 3)); // Request configuration mode, abort transmissions
    if (!waitForMode(MODE_CONFIGURATION, 10))
    {
        printf("MCP2517FD did not enter configuration mode\n");
        return false;
    }

    rawWriteRegister8(REG_INT + 2, 0x00);     // Disable RX/TX/TEF/MOD/TBC interrupts
    rawWriteRegister8(REG_INT + 3, (1 << 6)); // WAKIE only
    rawWriteRegister8(REG_INT + 1, 0x00);     // Clear pending flags (incl. WAKIF)
    rawWriteRegister8(REG_INT, 0x00);

    rawWriteRegister8(REG_CON + 3, MODE_SLEEP);
    unsigned long start = millis();
    // OSC.OSCDIS is set while in Sleep mode (LPMEN is left cleared, so SPI reads don't wake it)
    while ((rawReadRegister8(REG_OSC) & (1 << 2)) == 0 && currentMode() != MODE_SLEEP)
    {
        if (millis() - start > 10)
        {
            printf("MCP2517FD did not enter sleep mode\n");
            return false;
        }
    }

    gpio_hold_en((gpio_num_t)SS);
    return true;
}

void setup()
{
    gpio_hold_dis((gpio_num_t)SS); // Released from sleep hold, see sleep()
    pinMode(SS, OUTPUT);
    digitalWrite(SS, HIGH);
    SPI.begin(SCK, MISO, MOSI);
    wakeController();
    // ACAN2517FDSettings settings(ACAN2517FDSettings::OSC_20MHz, 500 * 1000, DataBitRateFactor::x4);
    // ACAN2517FDSettings settings(ACAN2517FDSettings::OSC_4MHz10xPLL, 500 * 1000, DataBitRateFactor::x4);
    ACAN2517FDSettings settings(ACAN2517FDSettings::OSC_40MHz, 500 * 1000, DataBitRateFactor::x4);
    // settings.mRequestedMode = ACAN2517FDSettings::InternalLoopBack; // Select loopback mode

    ACAN2517FDFilters filters;
    filters.appendFrameFilter(kStandard, 0x0CF, bmsInfoCallback);
    filters.appendFrameFilter(kStandard, 0x2AF, bmsInfo2Callback);
    filters.appendFrameFilter(kExtended, 0x12DD54D0, dynamicDischargeCallback);
    filters.appendFrameFilter(kExtended, 0x12DD54D1, batteryStatusCallback);
    filters.appendFrameFilter(kExtended, 0x12DD54D2, heatingStatusCallback);
    filters.appendFrameFilter(kExtended, 0x16A954A6, bmsSafetyCallback);
    filters.appendFrameFilter(kExtended, 0x17FE007B, diagnosticCallback);
    filters.appendFrameFilter(kExtended, 0x1A555551, thermalCallback);
    filters.appendFrameFilter(kExtended, 0x1A5555B2, chargingOptimizationCallback);
    // filters.appendFrameFilter(kExtended, 0x17FC007B, NULL);

    if (filters.filterStatus() != ACAN2517FDFilters::kFiltersOk)
    {
        printf("Error filter %d: %d\n", filters.filterErrorIndex(), filters.filterStatus());
    }

    settings.mDriverReceiveFIFOSize = 16;
    settings.mControllerReceiveFIFOSize = 16;
    //--- Configure regular transmit chain (used when message.idx == 0, default)
    settings.mDriverTransmitFIFOSize = 1;
    settings.mControllerTransmitFIFOSize = 1;
    settings.mControllerTransmitFIFORetransmissionAttempts = ACAN2517FDSettings::Disabled;
    //--- Configure TXQ transmit chain (used when message.idx == 255)
    settings.mControllerTXQSize = 1;
    settings.mControllerTXQBufferRetransmissionAttempts = ACAN2517FDSettings::Disabled;

    Serial.printf("MCP2517FD RAM Usage: %d bytes\n", settings.ramUsage());
    // 20MHz
    // settings.mDataPhaseSegment1 = 5;
    // settings.mDataPhaseSegment2 = 4;
    // settings.mDataSJW = 4;

    // 40MHz
    settings.mDataPhaseSegment1 = 10;
    settings.mDataPhaseSegment2 = 9;
    settings.mDataSJW = 9;

    const uint32_t errorCode = can.begin(settings, []
                                         { can.isr(); }, filters);
    if (errorCode == 0)
    {
#ifdef CAN_XSTBY
        // Let nINT0/GPIO0/XSTBY drive the transceiver STBY pin: low while the controller runs,
        // high (transceiver standby) while it is in Sleep mode. Requires STBY wired to GPIO0.
        uint8_t iocon0 = rawReadRegister8(REG_IOCON);
        rawWriteRegister8(REG_IOCON, (iocon0 & ~(1 << 0)) | (1 << 6)); // TRIS0 = 0 (output), XSTBYEN = 1
        uint8_t iocon1 = rawReadRegister8(REG_IOCON + 1);
        rawWriteRegister8(REG_IOCON + 1, iocon1 & ~(1 << 0)); // LAT0 = 0 (transceiver active outside Sleep)
#endif
        printf("Arbitration : %d / %d / %d (%d bits/s, SP %d%%)\n", settings.mArbitrationPhaseSegment1, settings.mArbitrationPhaseSegment2, settings.mArbitrationSJW, settings.actualArbitrationBitRate(), settings.arbitrationSamplePointFromBitStart());
        printf("Data phase  : %d / %d / %d (%d bits/s, SP %d%%)\n", settings.mDataPhaseSegment1, settings.mDataPhaseSegment2, settings.mDataSJW, settings.actualDataBitRate(), settings.dataSamplePointFromBitStart());
    }
    else
    {
        printf("Configuration error 0x%02x\n", errorCode);
    }
}

void setSending(bool sending)
{
    first = true;
    do_send = sending;
    do_send_updated_at = millis();
    if (heatingRequestStateCallback != nullptr)
    {
        heatingRequestStateCallback(sending);
    }
}

bool shouldBeHeating()
{
    if (!do_send) {
        return false;
    }
    unsigned long now = millis();
    // If we are DC charging or been heating for a long time, disable heating
    if (data.bms_mode == 6 || (millis() - do_send_updated_at > max_heating_time))
    {
        setSending(false);
    }
    return (
        (now - bms_info_last_update) < freshness_interval &&
        data.bms_mode != 0
        // &&
        // data.temperature_status_charge == 1
    );
}

void loop()
{
    CANFDMessage frame;
    unsigned long now = millis();
    if (now - latest_loop_run >= 500)
    {
        latest_loop_run = now;
        if (bms_info_last_update != -freshness_interval) {
            data.data_age = (uint32_t)((now - bms_info_last_update) / 1000);
        }
        // printf("Heating status: %d %d, %d%% (%d%%)\n", battery_heating_active, heating_request, power_battery_heating_watt, power_battery_heating_req_watt);
        // printf("Thermal status: %d %d\n", temperature_status_charge, performance_index_charge_peak_temperature_percentage);
        // printf("Predicted power: %d (%d)\n", max_charge_power_watt, max_charge_current_amp);
        // printf("Battery temp min/max: %d / %d\n", battery_min_temp, battery_max_temp);
        // printf("\n");
        if (shouldBeHeating())
        {
            CANMessage msg;
            msg.ext = true;
            frame.ext = true;
            frame.type = CANFDMessage::CANFD_WITH_BIT_RATE_SWITCH;
            frame.id = 0x17FC007B;
            msg.id = 0x17FC007B;
            frame.len = 8;
            msg.len = 8;
            if (first || session_error)
            {
                first = false;
                session_error = false;
                frame.data[0] = 0x02;
                frame.data[1] = 0x10;
                frame.data[2] = 0x03;
                frame.data[3] = 0x00;
                frame.data[4] = 0x00;
                frame.data[5] = 0x00;
                frame.data[6] = 0x00;
                frame.data[7] = 0x00;
            }
            else
            {
                frame.data[0] = 0x07;
                frame.data[1] = 0x2F;
                frame.data[2] = 0x80;
                frame.data[3] = 0x37;
                frame.data[4] = 0x03;
                frame.data[5] = 0x00;
                frame.data[6] = 0x10;
                frame.data[7] = 0x32;
            }
            can.tryToSend(frame);
        }
        updateData();
    }
    if (can.dispatchReceivedMessage())
    {
        last_activity = millis();
    }
}

}
