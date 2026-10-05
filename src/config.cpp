#include "config.h"
#include <wincrypt.h>

// Base registry key path
static const char* const REG_BASE_PATH = "Software\\HAMradio\\cm108ptt";

// Helper to extract filename from path
static void ExtractFilenameOnly(const char* fullPath, char* outName, DWORD maxLen) {
    if (!fullPath || !outName || maxLen == 0) return;
    const char* lastSlash = NULL;
    for (const char* p = fullPath; *p; ++p) {
        if (*p == '\\' || *p == '/') {
            lastSlash = p;
        }
    }
    const char* name = lastSlash ? (lastSlash + 1) : fullPath;
    lstrcpynA(outName, name, maxLen);
}

// Case-insensitive ends_with check
static bool EndsWithNoCase(const char* str, const char* suffix) {
    if (!str || !suffix) return false;
    int lenStr = lstrlenA(str);
    int lenSuf = lstrlenA(suffix);
    if (lenStr < lenSuf) return false;
    return (lstrcmpiA(str + (lenStr - lenSuf), suffix) == 0);
}

// Detect whether launched via .lnk or .exe and return the clean filename
void GetLaunchTargetFilename(char* outName, DWORD maxLen) {
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    GetStartupInfoA(&si);

    // If launched from a shell shortcut (.lnk), Windows sets lpTitle to the .lnk path
    if (si.lpTitle && EndsWithNoCase(si.lpTitle, ".lnk")) {
        ExtractFilenameOnly(si.lpTitle, outName, maxLen);
        return;
    }

    // Otherwise use executable filename
    char exePath[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exePath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        ExtractFilenameOnly(exePath, outName, maxLen);
        return;
    }

    lstrcpynA(outName, "cm108ptt.exe", maxLen);
}

// Compute hash of the filename using CryptoAPI MD5 (built into all Windows XP through 11).
// Includes FNV-1a fallback if CryptoAPI provider is unavailable.
void ComputeFileHash(const char* filename, char* outHash, DWORD maxLen) {
    char normalized[MAX_PATH];
    lstrcpynA(normalized, filename ? filename : "", MAX_PATH);
    CharLowerA(normalized);

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE hashBytes[16];
    DWORD hashLen = sizeof(hashBytes);
    bool cryptoSuccess = false;

    if (CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
            if (CryptHashData(hHash, (const BYTE*)normalized, (DWORD)lstrlenA(normalized), 0)) {
                if (CryptGetHashParam(hHash, HP_HASHVAL, hashBytes, &hashLen, 0)) {
                    cryptoSuccess = true;
                }
            }
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }

    if (cryptoSuccess && maxLen >= 33) {
        char* p = outHash;
        for (DWORD i = 0; i < hashLen; ++i) {
            wsprintfA(p, "%02x", hashBytes[i]);
            p += 2;
        }
        *p = '\0';
        return;
    }

    // Fallback: 32-bit FNV-1a hash
    unsigned long fnv = 2166136261u;
    for (const char* p = normalized; *p; ++p) {
        fnv ^= (unsigned char)(*p);
        fnv *= 16777619u;
    }
    wsprintfA(outHash, "%08lx", fnv);
}

// Save configuration to HKCU\Software\HAMradio\cm108ptt\<hash>
bool SaveSettingsToRegistry(const AppConfig& cfg) {
    if (!cfg.configHash[0]) return false;

    char subKey[256];
    wsprintfA(subKey, "%s\\%s", REG_BASE_PATH, cfg.configHash);

    HKEY hKey = NULL;
    DWORD disposition = 0;

    LONG res = RegCreateKeyExA(HKEY_CURRENT_USER, subKey, 0, NULL,
                               REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, &disposition);
    if (res != ERROR_SUCCESS) return false;

    // FilenameForSettings
    RegSetValueExA(hKey, "FilenameForSettings", 0, REG_SZ,
                   (const BYTE*)cfg.launchFilename,
                   (DWORD)(lstrlenA(cfg.launchFilename) + 1));

    // Serial port
    RegSetValueExA(hKey, "SerialPort", 0, REG_SZ,
                   (const BYTE*)cfg.serialPort,
                   (DWORD)(lstrlenA(cfg.serialPort) + 1));

    // Signal type
    DWORD sig = (DWORD)cfg.signalType;
    RegSetValueExA(hKey, "Signal", 0, REG_DWORD, (const BYTE*)&sig, sizeof(DWORD));

    // Invert signal
    DWORD inv = cfg.invertSignal ? 1 : 0;
    RegSetValueExA(hKey, "Invert", 0, REG_DWORD, (const BYTE*)&inv, sizeof(DWORD));

    // VID
    RegSetValueExA(hKey, "VID", 0, REG_SZ,
                   (const BYTE*)cfg.vidHex,
                   (DWORD)(lstrlenA(cfg.vidHex) + 1));

    // PID
    RegSetValueExA(hKey, "PID", 0, REG_SZ,
                   (const BYTE*)cfg.pidHex,
                   (DWORD)(lstrlenA(cfg.pidHex) + 1));

    // CM108 index
    DWORD idx = (DWORD)cfg.cm108Index;
    RegSetValueExA(hKey, "CM108Index", 0, REG_DWORD, (const BYTE*)&idx, sizeof(DWORD));

    // GPIO pin
    DWORD gpio = (DWORD)cfg.gpioPin;
    RegSetValueExA(hKey, "GPIO", 0, REG_DWORD, (const BYTE*)&gpio, sizeof(DWORD));

    // Polling interval
    DWORD interval = cfg.pollIntervalMs ? cfg.pollIntervalMs : 75;
    RegSetValueExA(hKey, "PollIntervalMs", 0, REG_DWORD, (const BYTE*)&interval, sizeof(DWORD));

    RegCloseKey(hKey);
    return true;
}

// Load configuration from HKCU\Software\HAMradio\cm108ptt\<hash>
bool LoadSettingsFromRegistry(AppConfig& cfg) {
    if (!cfg.configHash[0]) return false;

    char subKey[256];
    wsprintfA(subKey, "%s\\%s", REG_BASE_PATH, cfg.configHash);

    HKEY hKey = NULL;
    LONG res = RegOpenKeyExA(HKEY_CURRENT_USER, subKey, 0, KEY_READ, &hKey);
    if (res != ERROR_SUCCESS) {
        return false;
    }

    char strBuf[256];
    DWORD strSize;
    DWORD dwVal = 0;
    DWORD dwSize = sizeof(DWORD);
    DWORD dwType = 0;

    // Serial port
    strSize = sizeof(strBuf);
    if (RegQueryValueExA(hKey, "SerialPort", NULL, &dwType, (BYTE*)strBuf, &strSize) == ERROR_SUCCESS && dwType == REG_SZ) {
        lstrcpynA(cfg.serialPort, strBuf, sizeof(cfg.serialPort));
    }

    // Signal type
    dwSize = sizeof(DWORD);
    if (RegQueryValueExA(hKey, "Signal", NULL, &dwType, (BYTE*)&dwVal, &dwSize) == ERROR_SUCCESS && dwType == REG_DWORD) {
        cfg.signalType = (int)dwVal;
    }

    // Invert signal
    dwSize = sizeof(DWORD);
    if (RegQueryValueExA(hKey, "Invert", NULL, &dwType, (BYTE*)&dwVal, &dwSize) == ERROR_SUCCESS && dwType == REG_DWORD) {
        cfg.invertSignal = (dwVal != 0);
    }

    // VID
    strSize = sizeof(strBuf);
    if (RegQueryValueExA(hKey, "VID", NULL, &dwType, (BYTE*)strBuf, &strSize) == ERROR_SUCCESS && dwType == REG_SZ) {
        lstrcpynA(cfg.vidHex, strBuf, sizeof(cfg.vidHex));
    }

    // PID
    strSize = sizeof(strBuf);
    if (RegQueryValueExA(hKey, "PID", NULL, &dwType, (BYTE*)strBuf, &strSize) == ERROR_SUCCESS && dwType == REG_SZ) {
        lstrcpynA(cfg.pidHex, strBuf, sizeof(cfg.pidHex));
    }

    // CM108 index
    dwSize = sizeof(DWORD);
    if (RegQueryValueExA(hKey, "CM108Index", NULL, &dwType, (BYTE*)&dwVal, &dwSize) == ERROR_SUCCESS && dwType == REG_DWORD) {
        cfg.cm108Index = (int)dwVal;
    }

    // GPIO pin
    dwSize = sizeof(DWORD);
    if (RegQueryValueExA(hKey, "GPIO", NULL, &dwType, (BYTE*)&dwVal, &dwSize) == ERROR_SUCCESS && dwType == REG_DWORD) {
        cfg.gpioPin = (int)dwVal;
    }

    // Polling interval
    dwSize = sizeof(DWORD);
    if (RegQueryValueExA(hKey, "PollIntervalMs", NULL, &dwType, (BYTE*)&dwVal, &dwSize) == ERROR_SUCCESS && dwType == REG_DWORD) {
        cfg.pollIntervalMs = dwVal;
    }

    RegCloseKey(hKey);
    return true;
}
