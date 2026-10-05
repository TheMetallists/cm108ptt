#pragma once
#include <windows.h>

// Structure holding all runtime and persistent configuration
struct AppConfig {
    char serialPort[32];          // e.g. "COM3"
    int signalType;               // 0: DTR (DSR/CD), 1: RTS (CTS), 2: CTS, 3: DSR, 4: CD, 5: RI
    char vidHex[8];               // e.g. "0d8c"
    char pidHex[8];               // e.g. "0012"
    int cm108Index;               // 0-based card index
    int gpioPin;                  // 1, 2, 3, or 4 (default 3)
    bool invertSignal;            // true if signal is inverted
    DWORD pollIntervalMs;         // default 75 ms
    char launchFilename[MAX_PATH];// e.g. "cm108ptt.exe" or "rig1.lnk"
    char configHash[64];          // hex hash of launchFilename
};

// Signal types supported
enum SignalType {
    SIGNAL_DTR_DSR_CD = 0, // Peer DTR (monitored via DSR or CD/RLSD in com0com)
    SIGNAL_RTS_CTS    = 1, // Peer RTS (monitored via CTS in com0com)
    SIGNAL_CTS        = 2, // Direct CTS line
    SIGNAL_DSR        = 3, // Direct DSR line
    SIGNAL_CD         = 4, // Direct CD / RLSD line
    SIGNAL_RI         = 5  // Direct Ring Indicator line
};

// Functions to manage configuration and registry (CRT-free, pure Win32)
void GetLaunchTargetFilename(char* outName, DWORD maxLen);
void ComputeFileHash(const char* filename, char* outHash, DWORD maxLen);
bool LoadSettingsFromRegistry(AppConfig& cfg);
bool SaveSettingsToRegistry(const AppConfig& cfg);
