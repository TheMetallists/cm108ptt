#pragma once
#include <windows.h>
#include "config.h"

class SerialMonitor {
public:
    SerialMonitor();
    ~SerialMonitor();

    // Enumerate active serial ports from registry directly into the ComboBox
    static int EnumeratePorts(HWND hCombo);

    // Check if the given port name is present in the system
    static bool CheckPortExists(const char* portName);

    // Open the COM port
    bool Open(const char* portName);

    // Read the current state of the configured signal (returns true if asserted/active)
    bool ReadSignal(SignalType sigType, bool& error);

    // Close the COM port
    void Close();

    bool IsOpen() const { return m_hComm != INVALID_HANDLE_VALUE; }
    const char* GetPortName() const { return m_portName; }

private:
    HANDLE m_hComm;
    char m_portName[32];
};
