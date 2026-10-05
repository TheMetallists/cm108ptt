#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>

#include "config.h"
#include "cm108.h"
#include "serial_monitor.h"

// Control IDs
#define IDC_LABEL_PORT      101
#define IDC_COMBO_PORT      102
#define IDC_LABEL_SIGNAL    103
#define IDC_COMBO_SIGNAL    104
#define IDC_LABEL_INVERT    105
#define IDC_CHECK_INVERT    106
#define IDC_LABEL_VID       107
#define IDC_EDIT_VID        108
#define IDC_LABEL_PID       109
#define IDC_EDIT_PID        110
#define IDC_LABEL_INDEX     111
#define IDC_EDIT_INDEX      112
#define IDC_LABEL_GPIO      113
#define IDC_COMBO_GPIO      114
#define IDC_LABEL_STATUS    115
#define IDC_BTN_START       116
#define IDC_BTN_HELP        117

// Custom Windows Messages
#define WM_APP_PTT_CHANGED  (WM_APP + 1)
#define WM_APP_WORKER_ERROR (WM_APP + 2)

// Global state
static HINSTANCE g_hInstance = NULL;
static HWND g_hWndMain = NULL;
static HWND g_hComboPort = NULL;
static HWND g_hComboSignal = NULL;
static HWND g_hLabelInvert = NULL;
static HWND g_hCheckInvert = NULL;
static HWND g_hEditVid = NULL;
static HWND g_hEditPid = NULL;
static HWND g_hEditIndex = NULL;
static HWND g_hComboGpio = NULL;
static HWND g_hLabelStatus = NULL;
static HWND g_hBtnStart = NULL;
static HWND g_hBtnHelp = NULL;

static AppConfig g_config;
static CM108Device g_cm108;
static SerialMonitor g_serial;

static HANDLE g_hWorkerThread = NULL;
static HANDLE g_hStopEvent = NULL;
static bool g_isRunning = false;

// Signal choices
static const char* const SIGNAL_NAMES[] = {
    "DTR (DSR/CD)",
    "RTS (CTS)",
    "CTS",
    "DSR",
    "CD (RLSD)",
    "RI"
};
static const int SIGNAL_COUNT = sizeof(SIGNAL_NAMES) / sizeof(SIGNAL_NAMES[0]);

// GPIO pin choices
static const char* const GPIO_NAMES[] = {
    "GPIO 1",
    "GPIO 2",
    "GPIO 3",
    "GPIO 4"
};
static const int GPIO_COUNT = sizeof(GPIO_NAMES) / sizeof(GPIO_NAMES[0]);

// CRT-free hex string to WORD parser
static WORD HexStringToWord(const char* hexStr, WORD defaultVal) {
    if (!hexStr || !*hexStr) return defaultVal;
    WORD val = 0;
    for (const char* p = hexStr; *p; ++p) {
        char c = *p;
        val <<= 4;
        if (c >= '0' && c <= '9') val |= (WORD)(c - '0');
        else if (c >= 'a' && c <= 'f') val |= (WORD)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val |= (WORD)(c - 'A' + 10);
        else return defaultVal;
    }
    return val;
}

// CRT-free simple atoi
static int SimpleAtoi(const char* s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    int res = 0;
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res;
}

// Enable or disable UI editing controls
static void SetControlsEnabled(bool enabled) {
    EnableWindow(g_hComboPort, enabled ? TRUE : FALSE);
    EnableWindow(g_hComboSignal, enabled ? TRUE : FALSE);
    EnableWindow(g_hLabelInvert, enabled ? TRUE : FALSE);
    EnableWindow(g_hCheckInvert, enabled ? TRUE : FALSE);
    EnableWindow(g_hEditVid, enabled ? TRUE : FALSE);
    EnableWindow(g_hEditPid, enabled ? TRUE : FALSE);
    EnableWindow(g_hEditIndex, enabled ? TRUE : FALSE);
    EnableWindow(g_hComboGpio, enabled ? TRUE : FALSE);
}

// Background thread monitoring serial port and driving CM108 GPIO
static DWORD WINAPI MonitorWorkerThread(LPVOID lpParam) {
    AppConfig* cfg = (AppConfig*)lpParam;
    bool lastPttState = false;
    DWORD pollInterval = cfg->pollIntervalMs > 0 ? cfg->pollIntervalMs : 75;

    // Ensure transmitter is in unkeyed state at start of monitoring
    g_cm108.SetPTT(false);

    while (WaitForSingleObject(g_hStopEvent, pollInterval) == WAIT_TIMEOUT) {
        bool commError = false;
        bool rawActive = g_serial.ReadSignal((SignalType)cfg->signalType, commError);

        if (commError) {
            // Fail-safe: ensure PTT is unkeyed immediately on communication error
            g_cm108.SetPTT(false);
            PostMessageA(g_hWndMain, WM_APP_WORKER_ERROR, 0, 0);
            break;
        }

        // Logical PTT state requested by serial line (true = transmit, false = receive).
        // SetPTT internally translates this to the physical GPIO level honoring inversion.
        bool active = rawActive;

        if (active != lastPttState) {
            lastPttState = active;
            if (!g_cm108.SetPTT(active)) {
                // If CM108 write fails, shut down for safety
                PostMessageA(g_hWndMain, WM_APP_WORKER_ERROR, 0, 0);
                break;
            }
            PostMessageA(g_hWndMain, WM_APP_PTT_CHANGED, active ? 1 : 0, 0);
        }
    }

    // Safety guarantee: always unkey PTT honoring inversion when worker finishes
    g_cm108.SetPTT(false);
    return 0;
}

// Start monitoring
static bool StartMonitoring() {
    char buf[128];

    // Read Serial Port
    GetWindowTextA(g_hComboPort, g_config.serialPort, sizeof(g_config.serialPort));
    if (!g_config.serialPort[0]) {
        MessageBoxA(g_hWndMain, "Please select or enter a serial port.", "Input Error", MB_ICONWARNING);
        return false;
    }

    // Read Signal
    int sigSel = (int)SendMessageA(g_hComboSignal, CB_GETCURSEL, 0, 0);
    g_config.signalType = (sigSel >= 0 && sigSel < SIGNAL_COUNT) ? sigSel : 0;

    // Read Invert
    g_config.invertSignal = (SendMessageA(g_hCheckInvert, BM_GETCHECK, 0, 0) == BST_CHECKED);

    // Read VID & PID
    GetWindowTextA(g_hEditVid, g_config.vidHex, sizeof(g_config.vidHex));
    GetWindowTextA(g_hEditPid, g_config.pidHex, sizeof(g_config.pidHex));

    WORD vid = HexStringToWord(g_config.vidHex, 0x0D8C);
    WORD pid = HexStringToWord(g_config.pidHex, 0x0012);

    // Read CM108 index
    GetWindowTextA(g_hEditIndex, buf, sizeof(buf));
    g_config.cm108Index = SimpleAtoi(buf);
    if (g_config.cm108Index < 0) g_config.cm108Index = 0;

    // Read GPIO selection
    int gpioSel = (int)SendMessageA(g_hComboGpio, CB_GETCURSEL, 0, 0);
    g_config.gpioPin = (gpioSel >= 0 && gpioSel < GPIO_COUNT) ? (gpioSel + 1) : 3;

    // Default polling interval
    if (g_config.pollIntervalMs == 0) {
        g_config.pollIntervalMs = 75;
    }

    // Open CM108 HID device
    if (!g_cm108.Open(vid, pid, g_config.cm108Index, g_config.gpioPin, g_config.invertSignal)) {
        MessageBoxA(g_hWndMain,
                    "Failed to open CM108 HID device.\n"
                    "Please verify device connection, VID/PID, and index.",
                    "CM108 Error", MB_ICONERROR);
        return false;
    }

    // Open Serial Port
    if (!g_serial.Open(g_config.serialPort)) {
        g_cm108.Close();
        char err[128];
        wsprintfA(err, "Failed to open serial port: %s\nPlease verify port availability.", g_config.serialPort);
        MessageBoxA(g_hWndMain, err, "Serial Port Error", MB_ICONERROR);
        return false;
    }

    // Save settings to registry upon successful initialization
    SaveSettingsToRegistry(g_config);

    // Lock UI controls
    SetControlsEnabled(false);
    SetWindowTextA(g_hBtnStart, "STOP");
    SetWindowTextA(g_hLabelStatus, "Status: Monitoring [RX Standby]");

    // Spawn monitoring thread
    g_hStopEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    g_hWorkerThread = CreateThread(NULL, 0, MonitorWorkerThread, &g_config, 0, NULL);
    g_isRunning = true;

    return true;
}

// Stop monitoring
static void StopMonitoring() {
    if (!g_isRunning) return;

    if (g_hStopEvent) {
        SetEvent(g_hStopEvent);
    }

    if (g_hWorkerThread) {
        WaitForSingleObject(g_hWorkerThread, 1500);
        CloseHandle(g_hWorkerThread);
        g_hWorkerThread = NULL;
    }

    if (g_hStopEvent) {
        CloseHandle(g_hStopEvent);
        g_hStopEvent = NULL;
    }

    // CRITICAL FAIL-SAFE: deassert PTT and close devices
    g_cm108.Close();
    g_serial.Close();

    g_isRunning = false;
    SetControlsEnabled(true);
    SetWindowTextA(g_hBtnStart, "START");
    SetWindowTextA(g_hLabelStatus, "Status: Stopped");
}

// Populate UI controls from current configuration
static void PopulateUIFromConfig() {
    // Populate COM ports
    SendMessageA(g_hComboPort, CB_RESETCONTENT, 0, 0);
    int portCount = SerialMonitor::EnumeratePorts(g_hComboPort);

    // Select or enter configured port
    if (g_config.serialPort[0]) {
        int idx = (int)SendMessageA(g_hComboPort, CB_FINDSTRINGEXACT, -1, (LPARAM)g_config.serialPort);
        if (idx != CB_ERR) {
            SendMessageA(g_hComboPort, CB_SETCURSEL, idx, 0);
        } else {
            SetWindowTextA(g_hComboPort, g_config.serialPort);
        }
    } else if (portCount > 0) {
        SendMessageA(g_hComboPort, CB_SETCURSEL, 0, 0);
    }

    // Populate Signal dropdown
    SendMessageA(g_hComboSignal, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < SIGNAL_COUNT; ++i) {
        SendMessageA(g_hComboSignal, CB_ADDSTRING, 0, (LPARAM)SIGNAL_NAMES[i]);
    }
    int sigSel = (g_config.signalType >= 0 && g_config.signalType < SIGNAL_COUNT) ? g_config.signalType : 0;
    SendMessageA(g_hComboSignal, CB_SETCURSEL, sigSel, 0);

    // Invert Checkbox
    SendMessageA(g_hCheckInvert, BM_SETCHECK, g_config.invertSignal ? BST_CHECKED : BST_UNCHECKED, 0);

    // VID & PID
    SetWindowTextA(g_hEditVid, g_config.vidHex[0] ? g_config.vidHex : "0d8c");
    SetWindowTextA(g_hEditPid, g_config.pidHex[0] ? g_config.pidHex : "0012");

    // CM108 index
    char idxBuf[16];
    wsprintfA(idxBuf, "%d", g_config.cm108Index >= 0 ? g_config.cm108Index : 0);
    SetWindowTextA(g_hEditIndex, idxBuf);

    // GPIO dropdown
    SendMessageA(g_hComboGpio, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < GPIO_COUNT; ++i) {
        SendMessageA(g_hComboGpio, CB_ADDSTRING, 0, (LPARAM)GPIO_NAMES[i]);
    }
    int gpioIndex = (g_config.gpioPin >= 1 && g_config.gpioPin <= 4) ? (g_config.gpioPin - 1) : 2; // Default GPIO 3 (index 2)
    SendMessageA(g_hComboGpio, CB_SETCURSEL, gpioIndex, 0);
}

// Window Procedure
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

            // Row 1: Serial Port
            CreateWindowA("STATIC", "Serial port to monitor:",
                          WS_CHILD | WS_VISIBLE,
                          12, 14, 132, 18, hWnd, (HMENU)IDC_LABEL_PORT, g_hInstance, NULL);

            g_hComboPort = CreateWindowA("COMBOBOX", "",
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN | WS_VSCROLL,
                                         148, 11, 158, 160, hWnd, (HMENU)IDC_COMBO_PORT, g_hInstance, NULL);

            // Row 2: Signal & Invert (label on left, colon pointing directly to checkbox)
            CreateWindowA("STATIC", "Signal:",
                          WS_CHILD | WS_VISIBLE,
                          12, 44, 132, 18, hWnd, (HMENU)IDC_LABEL_SIGNAL, g_hInstance, NULL);

            g_hComboSignal = CreateWindowA("COMBOBOX", "",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                           148, 41, 90, 160, hWnd, (HMENU)IDC_COMBO_SIGNAL, g_hInstance, NULL);

            g_hLabelInvert = CreateWindowA("STATIC", "Invert:",
                                           WS_CHILD | WS_VISIBLE | SS_NOTIFY,
                                           246, 44, 38, 18, hWnd, (HMENU)IDC_LABEL_INVERT, g_hInstance, NULL);

            g_hCheckInvert = CreateWindowA("BUTTON", "",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                           287, 43, 19, 19, hWnd, (HMENU)IDC_CHECK_INVERT, g_hInstance, NULL);

            // Row 3: VID & PID
            CreateWindowA("STATIC", "VID:",
                          WS_CHILD | WS_VISIBLE,
                          12, 74, 132, 18, hWnd, (HMENU)IDC_LABEL_VID, g_hInstance, NULL);

            g_hEditVid = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "0d8c",
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                         148, 71, 55, 22, hWnd, (HMENU)IDC_EDIT_VID, g_hInstance, NULL);

            CreateWindowA("STATIC", "PID:",
                          WS_CHILD | WS_VISIBLE,
                          215, 74, 30, 18, hWnd, (HMENU)IDC_LABEL_PID, g_hInstance, NULL);

            g_hEditPid = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "0012",
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                         250, 71, 56, 22, hWnd, (HMENU)IDC_EDIT_PID, g_hInstance, NULL);

            // Row 4: CM108 index & GPIO pin
            CreateWindowA("STATIC", "cm108 #:",
                          WS_CHILD | WS_VISIBLE,
                          12, 104, 132, 18, hWnd, (HMENU)IDC_LABEL_INDEX, g_hInstance, NULL);

            g_hEditIndex = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "0",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
                                           148, 101, 40, 22, hWnd, (HMENU)IDC_EDIT_INDEX, g_hInstance, NULL);

            CreateWindowA("STATIC", "GPIO:",
                          WS_CHILD | WS_VISIBLE,
                          200, 104, 38, 18, hWnd, (HMENU)IDC_LABEL_GPIO, g_hInstance, NULL);

            g_hComboGpio = CreateWindowA("COMBOBOX", "",
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                         242, 101, 64, 120, hWnd, (HMENU)IDC_COMBO_GPIO, g_hInstance, NULL);

            // Row 5: Status Label
            g_hLabelStatus = CreateWindowA("STATIC", "Status: Idle",
                                           WS_CHILD | WS_VISIBLE | SS_LEFT,
                                           12, 134, 294, 18, hWnd, (HMENU)IDC_LABEL_STATUS, g_hInstance, NULL);

            // Row 6: START Button & Help [?] Button
            g_hBtnStart = CreateWindowA("BUTTON", "START",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                        99, 160, 120, 30, hWnd, (HMENU)IDC_BTN_START, g_hInstance, NULL);

            g_hBtnHelp = CreateWindowA("BUTTON", "?",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                       276, 160, 30, 30, hWnd, (HMENU)IDC_BTN_HELP, g_hInstance, NULL);

            // Set GUI font for all children
            HWND hChild = GetWindow(hWnd, GW_CHILD);
            while (hChild) {
                SendMessageA(hChild, WM_SETFONT, (WPARAM)hFont, TRUE);
                hChild = GetWindow(hChild, GW_HWNDNEXT);
            }
            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == IDC_BTN_START) {
                if (!g_isRunning) {
                    StartMonitoring();
                } else {
                    StopMonitoring();
                }
            } else if (wmId == IDC_BTN_HELP) {
                const char* helpText =
                    "COM to CM108 PTT bridge\n\n"
                    "This app converts virtual (like com0com)\n"
                    "com port signals to cm108 HID GPIO signals,\n"
                    "for apps that do not support cm108 directly.\n\n"
                    "Settings are saved in registry, under\n"
                    "HKCU\\Software\\HAMradio\\cm108ptt\\<hash>\n"
                    "with a key per filename (hash), so you can\n"
                    "create separate profiles using copies of the\n"
                    ".exe with different names or shortcuts (.lnk files)\n"
                    "pointing to the single .exe file -\n"
                    "a filename per profile.";
                MessageBoxA(hWnd, helpText, "About", MB_ICONINFORMATION | MB_OK);
            } else if (wmId == IDC_LABEL_INVERT) {
                if (!g_isRunning) {
                    LRESULT state = SendMessageA(g_hCheckInvert, BM_GETCHECK, 0, 0);
                    SendMessageA(g_hCheckInvert, BM_SETCHECK, (state == BST_CHECKED) ? BST_UNCHECKED : BST_CHECKED, 0);
                }
            }
            break;
        }

        case WM_APP_PTT_CHANGED: {
            bool active = (wParam != 0);
            if (active) {
                SetWindowTextA(g_hLabelStatus, "Status: Monitoring [TX Active]");
            } else {
                SetWindowTextA(g_hLabelStatus, "Status: Monitoring [RX Standby]");
            }
            break;
        }

        case WM_APP_WORKER_ERROR: {
            StopMonitoring();
            MessageBoxA(hWnd,
                        "Hardware communication error.\n"
                        "The serial port or CM108 device disconnected.\n"
                        "PTT has been released.",
                        "Device Error", MB_ICONWARNING);
            break;
        }

        case WM_CLOSE:
            StopMonitoring();
            DestroyWindow(hWnd);
            break;

        case WM_DESTROY:
            StopMonitoring();
            PostQuitMessage(0);
            break;

        case WM_QUERYENDSESSION:
        case WM_ENDSESSION:
            // Fail-safe: system shutdown or user logoff
            StopMonitoring();
            return TRUE;

        default:
            return DefWindowProcA(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    g_hInstance = hInstance;

    // Detect launch filename (.lnk or .exe) and generate config hash
    GetLaunchTargetFilename(g_config.launchFilename, sizeof(g_config.launchFilename));
    ComputeFileHash(g_config.launchFilename, g_config.configHash, sizeof(g_config.configHash));
    g_config.pollIntervalMs = 75; // Default polling interval
    lstrcpynA(g_config.vidHex, "0d8c", sizeof(g_config.vidHex));
    lstrcpynA(g_config.pidHex, "0012", sizeof(g_config.pidHex));
    g_config.gpioPin = 3;         // Default GPIO 3
    g_config.cm108Index = 0;
    g_config.signalType = 0;      // Default DTR (DSR/CD)
    g_config.invertSignal = false;// Default: not inverted

    // Load existing settings if stored in registry
    bool hasSavedSettings = LoadSettingsFromRegistry(g_config);

    // Register Window Class
    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    HICON hAppIcon = LoadIconA(hInstance, MAKEINTRESOURCEA(1));
    if (!hAppIcon) hAppIcon = LoadIconA(NULL, IDI_APPLICATION);
    wc.hIcon = hAppIcon;
    wc.hIconSm = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "CM108PTTWindowClass";

    if (!RegisterClassExA(&wc)) {
        return 1;
    }

    // Fixed small window size (client: 318 x 202)
    RECT rc = { 0, 0, 318, 202 };
    DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, dwStyle, FALSE);

    char title[300];
    wsprintfA(title, "CM108 PTT Bridge - [%s]", g_config.launchFilename);

    g_hWndMain = CreateWindowA("CM108PTTWindowClass", title,
                               dwStyle,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               rc.right - rc.left, rc.bottom - rc.top,
                               NULL, NULL, hInstance, NULL);

    if (!g_hWndMain) {
        return 1;
    }

    // Populate controls with loaded/default configuration
    PopulateUIFromConfig();

    ShowWindow(g_hWndMain, nCmdShow);
    UpdateWindow(g_hWndMain);

    // If loaded from registry, verify presence of configured hardware
    if (hasSavedSettings) {
        WORD vid = HexStringToWord(g_config.vidHex, 0x0D8C);
        WORD pid = HexStringToWord(g_config.pidHex, 0x0012);

        bool portExists = SerialMonitor::CheckPortExists(g_config.serialPort);
        bool cm108Exists = CM108Device::CheckDeviceExists(vid, pid, g_config.cm108Index);

        if (!portExists || !cm108Exists) {
            char msg[512];
            char portInfo[64] = "";
            char cmInfo[64] = "";

            if (!portExists) {
                wsprintfA(portInfo, " - Serial Port: %s (missing)\n", g_config.serialPort);
            }
            if (!cm108Exists) {
                wsprintfA(cmInfo, " - CM108 Device Index: #%d (missing)\n", g_config.cm108Index);
            }

            wsprintfA(msg, "Configured device(s) are not currently detected:\n%s%s\nSettings have been retained. If you connect the device now, you can press START.",
                      portInfo, cmInfo);

            MessageBoxA(g_hWndMain, msg, "Device Notice", MB_ICONINFORMATION | MB_OK);
        }
    }

    // Standard Message Loop
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageA(g_hWndMain, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    return (int)msg.wParam;
}

#if defined(_MSC_VER)
extern "C" {
    #pragma function(memset)
    void* memset(void* dest, int c, size_t count) {
        char* bytes = (char*)dest;
        while (count--) {
            *bytes++ = (char)c;
        }
        return dest;
    }

    #pragma function(memcpy)
    void* memcpy(void* dest, const void* src, size_t count) {
        char* d = (char*)dest;
        const char* s = (const char*)src;
        while (count--) {
            *d++ = *s++;
        }
        return dest;
    }
}
#endif

// Clean entry point when linked with Crinkler (/ENTRY:WinMainEntry) without CRT
extern "C" void WinMainEntry(void) {
    HINSTANCE hInst = GetModuleHandleA(NULL);
    STARTUPINFOA si;
    GetStartupInfoA(&si);
    int nCmdShow = (si.dwFlags & STARTF_USESHOWWINDOW) ? si.wShowWindow : SW_SHOWDEFAULT;
    int ret = WinMain(hInst, NULL, GetCommandLineA(), nCmdShow);
    ExitProcess((UINT)ret);
}

