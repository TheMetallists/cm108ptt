#include "cm108.h"
#include <setupapi.h>

extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}

// Case-insensitive substring search in pure C
static bool ContainsSubstrNoCase(const char* haystack, const char* needle) {
    if (!haystack || !needle) return false;
    int nlen = lstrlenA(needle);
    if (nlen == 0) return true;
    int hlen = lstrlenA(haystack);
    for (int i = 0; i <= hlen - nlen; ++i) {
        int j = 0;
        for (; j < nlen; ++j) {
            char c1 = haystack[i + j];
            char c2 = needle[j];
            if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
            if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
            if (c1 != c2) break;
        }
        if (j == nlen) return true;
    }
    return false;
}

CM108Device::CM108Device()
    : m_hDevice(INVALID_HANDLE_VALUE),
      m_gpioPin(3),
      m_invert(false),
      m_reportLength(5),
      m_gpioState(0) {
}

CM108Device::~CM108Device() {
    Close();
}

bool CM108Device::CheckDeviceExists(WORD vid, WORD pid, int index) {
    if (index < 0) return false;

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return false;
    }

    char vidPattern[32];
    char pidPattern[32];
    wsprintfA(vidPattern, "vid_%04x", vid);
    wsprintfA(pidPattern, "pid_%04x", pid);

    SP_DEVICE_INTERFACE_DATA devData;
    devData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    int matchCount = 0;
    bool found = false;

    BYTE detailBuffer[1024];
    SP_DEVICE_INTERFACE_DETAIL_DATA_A* detailData = (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)detailBuffer;
    detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &hidGuid, i, &devData); ++i) {
        DWORD requiredSize = 0;
        if (SetupDiGetDeviceInterfaceDetailA(hDevInfo, &devData, detailData, sizeof(detailBuffer), &requiredSize, NULL)) {
            if (ContainsSubstrNoCase(detailData->DevicePath, vidPattern) &&
                ContainsSubstrNoCase(detailData->DevicePath, pidPattern)) {
                if (matchCount == index) {
                    found = true;
                    break;
                }
                matchCount++;
            }
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return found;
}

bool CM108Device::Open(WORD vid, WORD pid, int index, int gpioPin, bool invert) {
    Close();

    if (index < 0) return false;
    m_invert = invert;

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return false;
    }

    char vidPattern[32];
    char pidPattern[32];
    wsprintfA(vidPattern, "vid_%04x", vid);
    wsprintfA(pidPattern, "pid_%04x", pid);

    SP_DEVICE_INTERFACE_DATA devData;
    devData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    int matchCount = 0;
    char targetPath[1024] = {0};

    BYTE detailBuffer[1024];
    SP_DEVICE_INTERFACE_DETAIL_DATA_A* detailData = (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)detailBuffer;
    detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &hidGuid, i, &devData); ++i) {
        DWORD requiredSize = 0;
        if (SetupDiGetDeviceInterfaceDetailA(hDevInfo, &devData, detailData, sizeof(detailBuffer), &requiredSize, NULL)) {
            if (ContainsSubstrNoCase(detailData->DevicePath, vidPattern) &&
                ContainsSubstrNoCase(detailData->DevicePath, pidPattern)) {
                if (matchCount == index) {
                    lstrcpynA(targetPath, detailData->DevicePath, sizeof(targetPath));
                    break;
                }
                matchCount++;
            }
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);

    if (!targetPath[0]) {
        return false;
    }

    m_gpioPin = (gpioPin >= 1 && gpioPin <= 4) ? gpioPin : 3;

    // Attempt to open device with read/write access
    m_hDevice = CreateFileA(targetPath,
                            GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL,
                            OPEN_EXISTING,
                            0,
                            NULL);

    // Fallback: non-exclusive query access
    if (m_hDevice == INVALID_HANDLE_VALUE) {
        m_hDevice = CreateFileA(targetPath,
                                0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL,
                                OPEN_EXISTING,
                                0,
                                NULL);
    }

    if (m_hDevice == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Determine output report length via HID capabilities
    m_reportLength = 5;
    PHIDP_PREPARSED_DATA preparsedData = NULL;
    if (HidD_GetPreparsedData(m_hDevice, &preparsedData)) {
        HIDP_CAPS caps;
        if (HidP_GetCaps(preparsedData, &caps) == HIDP_STATUS_SUCCESS) {
            if (caps.OutputReportByteLength > 0) {
                m_reportLength = caps.OutputReportByteLength;
            }
        }
        HidD_FreePreparsedData(preparsedData);
    }

    // Fail-safe: ensure PTT is immediately UNKEYED (honoring inversion) upon opening
    m_gpioState = 0;
    SetPTT(false);

    return true;
}

bool CM108Device::SendReport(BYTE iodata, BYTE iomask) {
    if (m_hDevice == INVALID_HANDLE_VALUE) return false;

    BYTE buf[16] = {0};
    buf[0] = 0x00; // Report ID

    if (m_reportLength >= 5) {
        buf[1] = 0x00;
        buf[2] = iodata;
        buf[3] = iomask;
        buf[4] = 0x00;
    } else {
        buf[1] = iodata;
        buf[2] = iomask;
        buf[3] = 0x00;
    }

    DWORD len = (m_reportLength > 0 && m_reportLength <= sizeof(buf)) ? m_reportLength : 5;

    // 1. Try HidD_SetOutputReport
    if (HidD_SetOutputReport(m_hDevice, buf, len)) {
        return true;
    }

    // 2. Try WriteFile
    DWORD written = 0;
    if (WriteFile(m_hDevice, buf, len, &written, NULL) && written == len) {
        return true;
    }

    // 3. Try HidD_SetFeature as fallback for certain clones
    if (HidD_SetFeature(m_hDevice, buf, len)) {
        return true;
    }

    return false;
}

bool CM108Device::SetGpioLevel(bool high) {
    if (m_hDevice == INVALID_HANDLE_VALUE) return false;

    BYTE pinBit = (BYTE)(1 << (m_gpioPin - 1));
    BYTE iomask = pinBit;

    if (high) {
        m_gpioState |= pinBit;
    } else {
        m_gpioState &= (BYTE)(~pinBit);
    }

    return SendReport(m_gpioState, iomask);
}

bool CM108Device::SetPTT(bool active) {
    // By default (m_invert == false), LOW level on GPIO triggers PTT:
    // - active (keyed / TX)   -> GPIO LOW (false)
    // - inactive (unkey / RX) -> GPIO HIGH (true)
    // When inverted (m_invert == true):
    // - active (keyed / TX)   -> GPIO HIGH (true)
    // - inactive (unkey / RX) -> GPIO LOW (false)
    bool gpioHigh = m_invert ? active : !active;
    return SetGpioLevel(gpioHigh);
}

void CM108Device::Close() {
    if (m_hDevice != INVALID_HANDLE_VALUE) {
        // Leave PTT unkeyed honoring inversion before closing handle!
        SetPTT(false);
        CloseHandle(m_hDevice);
        m_hDevice = INVALID_HANDLE_VALUE;
    }
}
