#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <iphlpapi.h>
#include <cwchar>
#include <cstdio>
#include <cstdlib>

#pragma comment(lib, "IPHLPAPI.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Advapi32.lib")

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1000
#define ID_TIMER_UPDATE 1

NOTIFYICONDATAW nidDown = {sizeof(NOTIFYICONDATAW)};
NOTIFYICONDATAW nidUp = {sizeof(NOTIFYICONDATAW)};
bool showDownIcon = true;
bool showUpIcon = true;

unsigned long long prevDown = 0;
unsigned long long prevUp = 0;
unsigned long long startDown = 0;
unsigned long long startUp = 0;
unsigned long long peakDown = 0;
unsigned long long peakUp = 0;

void FormatSpeedC(unsigned long long bytes, wchar_t *outBuf, size_t bufSize)
{
    double speed = (double)bytes;
    const wchar_t *units[] = {L"B/s", L"KB/s", L"MB/s", L"GB/s"};
    int unitIndex = 0;
    while (speed >= 1024 && unitIndex < 3)
    {
        speed /= 1024;
        unitIndex++;
    }

    if (unitIndex == 0)
        _snwprintf(outBuf, bufSize, L"%.0f %s", speed, units[unitIndex]);
    else if (speed >= 100)
        _snwprintf(outBuf, bufSize, L"%.0f %s", speed, units[unitIndex]);
    else
        _snwprintf(outBuf, bufSize, L"%.1f %s", speed, units[unitIndex]);
}

const unsigned char font4x12[10][12] = {
    {15, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 15},  // 0
    {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2},    // 1
    {15, 1, 1, 1, 1, 15, 8, 8, 8, 8, 8, 15}, // 2
    {15, 1, 1, 1, 1, 15, 1, 1, 1, 1, 1, 15}, // 3
    {9, 9, 9, 9, 9, 15, 1, 1, 1, 1, 1, 1},   // 4
    {15, 8, 8, 8, 8, 15, 1, 1, 1, 1, 1, 15}, // 5
    {15, 8, 8, 8, 8, 15, 9, 9, 9, 9, 9, 15}, // 6
    {15, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},   // 7
    {15, 9, 9, 9, 9, 15, 9, 9, 9, 9, 9, 15}, // 8
    {15, 9, 9, 9, 9, 15, 1, 1, 1, 1, 1, 15}  // 9
};

void FillRectLogical(DWORD *pixels, int lx, int ly, int lw, int lh, int iconSize, DWORD color)
{
    int xStart = lx * iconSize / 16;
    int xEnd = (lx + lw) * iconSize / 16;
    int yStart = ly * iconSize / 16;
    int yEnd = (ly + lh) * iconSize / 16;
    if (xStart < 0)
        xStart = 0;
    if (yStart < 0)
        yStart = 0;
    if (xEnd > iconSize)
        xEnd = iconSize;
    if (yEnd > iconSize)
        yEnd = iconSize;
    for (int y = yStart; y < yEnd; ++y)
    {
        for (int x = xStart; x < xEnd; ++x)
        {
            pixels[y * iconSize + x] = color;
        }
    }
}

void DrawDigit(DWORD *pixels, int startX, int digit, int iconSize, DWORD color)
{
    if (digit < 0 || digit > 9)
        return;
    for (int ly = 0; ly < 12; ++ly)
    {
        unsigned char row = font4x12[digit][ly];
        for (int lx = 0; lx < 4; ++lx)
        {
            if (row & (1 << (3 - lx)))
            {
                FillRectLogical(pixels, startX + lx, ly + 4, 1, 1, iconSize, color);
            }
        }
    }
}

bool IsTaskbarDarkMode()
{
    HKEY hKey;
    bool isDark = true; // Default to dark mode (white text)
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD type;
        DWORD val = 0;
        DWORD size = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"SystemUsesLightTheme", NULL, &type, (BYTE *)&val, &size) == ERROR_SUCCESS)
        {
            isDark = (val == 0);
        }
        RegCloseKey(hKey);
    }
    return isDark;
}

// Generates a 32-bit ARGB fully transparent icon
HICON CreateSpeedIconC(unsigned long long speedBytes, bool isDownload)
{
    int iconSize = GetSystemMetrics(SM_CXSMICON);
    HDC hdc = GetDC(NULL);

    // Create 32-bit DIB Section for true Alpha blending
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = iconSize;
    bmi.bmiHeader.biHeight = -iconSize; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    DWORD *pixels = NULL;
    HBITMAP hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);

    for (int i = 0; i < iconSize * iconSize; ++i)
    {
        pixels[i] = 0x00000000;
    }

    double speed = (double)speedBytes;
    int unitIndex = 0;
    while (speed >= 1024 && unitIndex < 3)
    {
        speed /= 1024.0;
        unitIndex++;
    }

    DWORD unitColor = isDownload ? 0xFF00FF00 : 0xFFFF0000;

    if (unitIndex == 0)
    { // B
        FillRectLogical(pixels, 0, 0, 16, 2, iconSize, unitColor);
    }
    else
    {
        if (unitIndex >= 1)
            FillRectLogical(pixels, 0, 0, 4, 3, iconSize, unitColor);
        if (unitIndex >= 2)
            FillRectLogical(pixels, 5, 0, 4, 3, iconSize, unitColor);
        if (unitIndex >= 3)
            FillRectLogical(pixels, 10, 0, 4, 3, iconSize, unitColor);
    }

    unsigned int val = (unsigned int)speed;
    if (val > 999)
        val = 999;

    int d1 = val / 100;
    int d2 = (val / 10) % 10;
    int d3 = val % 10;

    bool showD1 = (d1 > 0);
    bool showD2 = (d1 > 0 || d2 > 0);
    bool showD3 = true;

    bool isDark = IsTaskbarDarkMode();
    DWORD textColor = isDark ? 0xFFFFFFFF : 0xFF000000;

    if (showD1)
        DrawDigit(pixels, 0, d1, iconSize, textColor);
    if (showD2)
        DrawDigit(pixels, 6, d2, iconSize, textColor);
    if (showD3)
        DrawDigit(pixels, 12, d3, iconSize, textColor);

    HDC hMaskDC = CreateCompatibleDC(hdc);
    HBITMAP hMask = CreateBitmap(iconSize, iconSize, 1, 1, NULL);
    HBITMAP hOldMask = (HBITMAP)SelectObject(hMaskDC, hMask);
    RECT rect = {0, 0, iconSize, iconSize};
    FillRect(hMaskDC, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH)); // Solid black mask
    SelectObject(hMaskDC, hOldMask);
    DeleteDC(hMaskDC);

    ICONINFO ii = {0};
    ii.fIcon = TRUE;
    ii.hbmMask = hMask;
    ii.hbmColor = hBitmap;
    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hBitmap);
    DeleteObject(hMask);
    ReleaseDC(NULL, hdc);

    return hIcon;
}

void GetNetworkTraffic(unsigned long long &down, unsigned long long &up, bool calcDown, bool calcUp)
{
    unsigned long long tempDown = 0;
    unsigned long long tempUp = 0;
    if (!calcDown && !calcUp)
        return;

    ULONG dwSize = 0;
    if (GetIfTable(NULL, &dwSize, FALSE) == ERROR_INSUFFICIENT_BUFFER)
    {
        PMIB_IFTABLE pIfTable = (PMIB_IFTABLE)malloc(dwSize);
        if (pIfTable)
        {
            if (GetIfTable(pIfTable, &dwSize, FALSE) == NO_ERROR)
            {
                for (DWORD i = 0; i < pIfTable->dwNumEntries; i++)
                {
                    MIB_IFROW &row = pIfTable->table[i];
                    if (row.dwOperStatus == MIB_IF_OPER_STATUS_OPERATIONAL && row.dwType != 24)
                    { // 24 is loopback
                        // Note: Virtual/tunnel adapters (VPNs, WSL, Hyper-V) are also included here and may double-count physical traffic.
                        if (calcDown)
                            tempDown += (unsigned long long)row.dwInOctets;
                        if (calcUp)
                            tempUp += (unsigned long long)row.dwOutOctets;
                    }
                }
            }
            free(pIfTable);
        }
    }
    if (calcDown)
        down = tempDown;
    if (calcUp)
        up = tempUp;
}

void SetAutoStart(bool enable)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        if (enable)
        {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(NULL, path, MAX_PATH);
            RegSetValueExW(hKey, L"InternetSpeedMonitor", 0, REG_SZ, (BYTE *)path, (wcslen(path) + 1) * sizeof(wchar_t));
        }
        else
        {
            RegDeleteValueW(hKey, L"InternetSpeedMonitor");
        }
        RegCloseKey(hKey);
    }
}

HHOOK hMsgBoxHook = NULL;
LRESULT CALLBACK MsgBoxCBTProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HCBT_ACTIVATE)
    {
        HWND hDlg = (HWND)wParam;
        SetDlgItemTextW(hDlg, IDYES, L"Both");
        SetDlgItemTextW(hDlg, IDNO, L"Down");
        SetDlgItemTextW(hDlg, IDCANCEL, L"Up");
        UnhookWindowsHookEx(hMsgBoxHook);
        hMsgBoxHook = NULL;
    }
    return CallNextHookEx(hMsgBoxHook, nCode, wParam, lParam);
}

void LoadOrAskSettings()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\InternetSpeedMonitor", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD type, val, size = sizeof(DWORD);
        bool configured = false;
        if (RegQueryValueExW(hKey, L"Configured", NULL, &type, (BYTE *)&val, &size) == ERROR_SUCCESS && val == 1)
        {
            configured = true;
            size = sizeof(DWORD);
            if (RegQueryValueExW(hKey, L"ShowDown", NULL, &type, (BYTE *)&val, &size) == ERROR_SUCCESS)
                showDownIcon = (val != 0);
            size = sizeof(DWORD);
            if (RegQueryValueExW(hKey, L"ShowUp", NULL, &type, (BYTE *)&val, &size) == ERROR_SUCCESS)
                showUpIcon = (val != 0);
        }
        RegCloseKey(hKey);
        if (configured)
            return;
    }

    int autoStart = MessageBoxW(NULL, L"Do you want this application to automatically start on system startup?", L"Startup Configuration", MB_YESNO | MB_ICONQUESTION);
    SetAutoStart(autoStart == IDYES);

    hMsgBoxHook = SetWindowsHookExW(WH_CBT, MsgBoxCBTProc, NULL, GetCurrentThreadId());
    int choice = MessageBoxW(NULL, L"Which icons should show?\n\n[Both] = Both Download and Upload\n[Down] = Only Download\n[Up] = Only Upload", L"Icon Selection", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (hMsgBoxHook)
    {
        UnhookWindowsHookEx(hMsgBoxHook);
        hMsgBoxHook = NULL;
    }

    if (choice == IDYES)
    {
        showDownIcon = true;
        showUpIcon = true;
    }
    else if (choice == IDNO)
    {
        showDownIcon = true;
        showUpIcon = false;
    }
    else
    {
        showDownIcon = false;
        showUpIcon = true;
    }

    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\InternetSpeedMonitor", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        DWORD val = 1;
        RegSetValueExW(hKey, L"Configured", 0, REG_DWORD, (BYTE *)&val, sizeof(DWORD));
        val = showDownIcon ? 1 : 0;
        RegSetValueExW(hKey, L"ShowDown", 0, REG_DWORD, (BYTE *)&val, sizeof(DWORD));
        val = showUpIcon ? 1 : 0;
        RegSetValueExW(hKey, L"ShowUp", 0, REG_DWORD, (BYTE *)&val, sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_TIMER:
        if (wParam == ID_TIMER_UPDATE)
        {
            unsigned long long currDown = prevDown, currUp = prevUp;
            GetNetworkTraffic(currDown, currUp, showDownIcon, showUpIcon);

            if (showDownIcon)
            {
                unsigned long long dBytes = (currDown >= prevDown) ? (currDown - prevDown) : 0;
                if (dBytes > peakDown)
                    peakDown = dBytes;

                wchar_t speedBuf[32], peakBuf[32], totalBuf[32];
                FormatSpeedC(dBytes, speedBuf, 32);
                FormatSpeedC(peakDown, peakBuf, 32);
                FormatSpeedC((currDown >= startDown) ? (currDown - startDown) : 0, totalBuf, 32);

                HICON hNewIcon = CreateSpeedIconC(dBytes, true);
                HICON hOldIcon = nidDown.hIcon;
                nidDown.hIcon = hNewIcon;

                _snwprintf(nidDown.szTip, sizeof(nidDown.szTip) / sizeof(wchar_t), L"Download: %s\nPeak: %s\nTotal: %s", speedBuf, peakBuf, totalBuf);
                Shell_NotifyIconW(NIM_MODIFY, &nidDown);
                if (hOldIcon)
                    DestroyIcon(hOldIcon);
            }

            if (showUpIcon)
            {
                unsigned long long uBytes = (currUp >= prevUp) ? (currUp - prevUp) : 0;
                if (uBytes > peakUp)
                    peakUp = uBytes;

                wchar_t speedBuf[32], peakBuf[32], totalBuf[32];
                FormatSpeedC(uBytes, speedBuf, 32);
                FormatSpeedC(peakUp, peakBuf, 32);
                FormatSpeedC((currUp >= startUp) ? (currUp - startUp) : 0, totalBuf, 32);

                HICON hNewIcon = CreateSpeedIconC(uBytes, false);
                HICON hOldIcon = nidUp.hIcon;
                nidUp.hIcon = hNewIcon;

                _snwprintf(nidUp.szTip, sizeof(nidUp.szTip) / sizeof(wchar_t), L"Upload: %s\nPeak: %s\nTotal: %s", speedBuf, peakBuf, totalBuf);
                Shell_NotifyIconW(NIM_MODIFY, &nidUp);
                if (hOldIcon)
                    DestroyIcon(hOldIcon);
            }

            if (showDownIcon)
                prevDown = currDown;
            if (showUpIcon)
                prevUp = currUp;
        }
        break;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP)
        {
            int iconId = (int)wParam; // 1 for down, 2 for up
            POINT curPoint;
            GetCursorPos(&curPoint);
            HMENU hMenu = CreatePopupMenu();
            if (hMenu)
            {
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT + iconId, L"Exit");
                SetForegroundWindow(hWnd);
                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, curPoint.x, curPoint.y, 0, hWnd, NULL);
                DestroyMenu(hMenu);
            }
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_EXIT + 1)
        { // Exit Down
            showDownIcon = false;
            Shell_NotifyIconW(NIM_DELETE, &nidDown);
            if (nidDown.hIcon)
                DestroyIcon(nidDown.hIcon);
            nidDown.hIcon = NULL;
            if (!showDownIcon && !showUpIcon)
                PostQuitMessage(0);
        }
        else if (LOWORD(wParam) == ID_TRAY_EXIT + 2)
        { // Exit Up
            showUpIcon = false;
            Shell_NotifyIconW(NIM_DELETE, &nidUp);
            if (nidUp.hIcon)
                DestroyIcon(nidUp.hIcon);
            nidUp.hIcon = NULL;
            if (!showDownIcon && !showUpIcon)
                PostQuitMessage(0);
        }
        break;

    case WM_DESTROY:
        KillTimer(hWnd, ID_TIMER_UPDATE);
        if (showDownIcon)
        {
            Shell_NotifyIconW(NIM_DELETE, &nidDown);
            if (nidDown.hIcon)
                DestroyIcon(nidDown.hIcon);
        }
        if (showUpIcon)
        {
            Shell_NotifyIconW(NIM_DELETE, &nidUp);
            if (nidUp.hIcon)
                DestroyIcon(nidUp.hIcon);
        }
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"SpeedMonitorLiteMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        return 0; // Already running
    }

    LoadOrAskSettings();

    if (!showDownIcon && !showUpIcon)
    {
        if (hMutex)
        {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
        }
        return 0; // Nothing to show
    }

    const wchar_t *szClassName = L"SpeedMonitor";

    WNDCLASSEXW wcex = {sizeof(WNDCLASSEXW)};
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.lpszClassName = szClassName;
    wcex.hIcon = NULL;
    wcex.hIconSm = NULL;

    if (!RegisterClassExW(&wcex))
    {
        if (hMutex)
        {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
        }
        return 1;
    }

    HWND hWnd = CreateWindowExW(0, szClassName, L"SpeedMonitor", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
    if (!hWnd)
    {
        if (hMutex)
        {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
        }
        return 1;
    }

    GetNetworkTraffic(prevDown, prevUp, showDownIcon, showUpIcon);
    startDown = prevDown;
    startUp = prevUp;

    if (showDownIcon)
    {
        nidDown.hWnd = hWnd;
        nidDown.uID = 1;
        nidDown.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nidDown.uCallbackMessage = WM_TRAYICON;
        nidDown.hIcon = CreateSpeedIconC(0, true);
        nidDown.uVersion = NOTIFYICON_VERSION_4;
        wcscpy(nidDown.szTip, L"Download Init...");
        Shell_NotifyIconW(NIM_ADD, &nidDown);
        Shell_NotifyIconW(NIM_SETVERSION, &nidDown);
    }

    if (showUpIcon)
    {
        nidUp.hWnd = hWnd;
        nidUp.uID = 2;
        nidUp.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nidUp.uCallbackMessage = WM_TRAYICON;
        nidUp.hIcon = CreateSpeedIconC(0, false);
        nidUp.uVersion = NOTIFYICON_VERSION_4;
        wcscpy(nidUp.szTip, L"Upload Init...");
        Shell_NotifyIconW(NIM_ADD, &nidUp);
        Shell_NotifyIconW(NIM_SETVERSION, &nidUp);
    }

    SetTimer(hWnd, ID_TIMER_UPDATE, 1000, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hMutex)
    {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
    return (int)msg.wParam;
}
