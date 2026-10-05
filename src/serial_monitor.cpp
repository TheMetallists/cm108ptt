#include "serial_monitor.h"

SerialMonitor::SerialMonitor()
    : m_hComm(INVALID_HANDLE_VALUE) {
    m_portName[0] = '\0';
}

SerialMonitor::~SerialMonitor() {
    Close();
}

int SerialMonitor::EnumeratePorts(HWND hCombo) {
    int count = 0;
    HKEY hKey = NULL;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char valName[256];
        BYTE valData[256];
        DWORD valIndex = 0;

        while (true) {
            DWORD nameSize = sizeof(valName);
            DWORD dataSize = sizeof(valData);
            DWORD type = 0;

            LONG res = RegEnumValueA(hKey, valIndex++, valName, &nameSize, NULL, &type, valData, &dataSize);
            if (res != ERROR_SUCCESS) break;

            if (type == REG_SZ && dataSize > 0) {
                const char* port = (const char*)valData;
                if (port[0]) {
                    SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)port);
                    count++;
                }
            }
        }
        RegCloseKey(hKey);
    }

    return count;
}

bool SerialMonitor::CheckPortExists(const char* portName) {
    if (!portName || !portName[0]) return false;

    HKEY hKey = NULL;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char valName[256];
        BYTE valData[256];
        DWORD valIndex = 0;

        while (true) {
            DWORD nameSize = sizeof(valName);
            DWORD dataSize = sizeof(valData);
            DWORD type = 0;

            LONG res = RegEnumValueA(hKey, valIndex++, valName, &nameSize, NULL, &type, valData, &dataSize);
            if (res != ERROR_SUCCESS) break;

            if (type == REG_SZ && dataSize > 0) {
                if (lstrcmpiA(portName, (const char*)valData) == 0) {
                    RegCloseKey(hKey);
                    return true;
                }
            }
        }
        RegCloseKey(hKey);
    }
    return false;
}

bool SerialMonitor::Open(const char* portName) {
    Close();

    if (!portName || !portName[0]) return false;

    char devicePath[64];
    if (portName[0] == '\\' && portName[1] == '\\') {
        lstrcpynA(devicePath, portName, sizeof(devicePath));
    } else {
        wsprintfA(devicePath, "\\\\.\\%s", portName);
    }

    m_hComm = CreateFileA(devicePath,
                          GENERIC_READ | GENERIC_WRITE,
                          0,
                          NULL,
                          OPEN_EXISTING,
                          0,
                          NULL);

    if (m_hComm == INVALID_HANDLE_VALUE) {
        m_hComm = CreateFileA(devicePath,
                              GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL,
                              OPEN_EXISTING,
                              0,
                              NULL);
    }

    if (m_hComm == INVALID_HANDLE_VALUE) {
        return false;
    }

    COMMTIMEOUTS timeouts;
    ZeroMemory(&timeouts, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(m_hComm, &timeouts);

    lstrcpynA(m_portName, portName, sizeof(m_portName));
    return true;
}

bool SerialMonitor::ReadSignal(SignalType sigType, bool& error) {
    if (m_hComm == INVALID_HANDLE_VALUE) {
        error = true;
        return false;
    }

    DWORD modemStatus = 0;
    if (!GetCommModemStatus(m_hComm, &modemStatus)) {
        error = true;
        return false;
    }

    error = false;

    switch (sigType) {
        case SIGNAL_DTR_DSR_CD:
            return (modemStatus & (MS_DSR_ON | MS_RLSD_ON)) != 0;

        case SIGNAL_RTS_CTS:
            return (modemStatus & MS_CTS_ON) != 0;

        case SIGNAL_CTS:
            return (modemStatus & MS_CTS_ON) != 0;

        case SIGNAL_DSR:
            return (modemStatus & MS_DSR_ON) != 0;

        case SIGNAL_CD:
            return (modemStatus & MS_RLSD_ON) != 0;

        case SIGNAL_RI:
            return (modemStatus & MS_RING_ON) != 0;

        default:
            return false;
    }
}

void SerialMonitor::Close() {
    if (m_hComm != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hComm);
        m_hComm = INVALID_HANDLE_VALUE;
    }
    m_portName[0] = '\0';
}
