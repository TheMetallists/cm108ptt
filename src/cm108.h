#pragma once
#include <windows.h>

class CM108Device {
public:
    CM108Device();
    ~CM108Device();

    // Check if the device exists without holding it open
    static bool CheckDeviceExists(WORD vid, WORD pid, int index);

    // Open device at specified index with configured GPIO pin and inversion setting
    bool Open(WORD vid, WORD pid, int index, int gpioPin, bool invert = false);

    // Inversion setting (false = default active-low PTT, true = active-high PTT)
    void SetInvert(bool invert) { m_invert = invert; }
    bool GetInvert() const { return m_invert; }

    // Set PTT active state (true = transmit/keyed, false = receive/unkeyed)
    // Honors inversion:
    // - Default (invert = false): active -> GPIO LOW, unkeyed -> GPIO HIGH
    // - Inverted (invert = true):  active -> GPIO HIGH, unkeyed -> GPIO LOW
    bool SetPTT(bool active);

    // Set raw GPIO pin level directly (true = HIGH, false = LOW)
    bool SetGpioLevel(bool high);

    // Immediate fail-safe: leave PTT unkeyed honoring inversion and close handle
    void Close();

    bool IsOpen() const { return m_hDevice != INVALID_HANDLE_VALUE; }
    int GetGpioPin() const { return m_gpioPin; }

private:
    HANDLE m_hDevice;
    int m_gpioPin;
    bool m_invert;
    WORD m_reportLength;
    BYTE m_gpioState;

    bool SendReport(BYTE iodata, BYTE iomask);
};
