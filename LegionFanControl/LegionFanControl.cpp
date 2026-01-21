/*
 * =================================================================================
 * Legion EC Writer - A tool to modify the fan curve on ITE-5507 ECs
 * =================================================================================
 *
 * This program uses the WinIO driver to directly write a custom fan curve
 * to the Embedded Controller's extended RAM. It includes a "Silent" profile
 * and the ability to restore the factory default profile discovered from your dump.
 *
 * COMPILE: Link with WinIox64.lib and ensure winio.h is in your include path.
 * RUN:     Place the compiled .exe in the same folder as WinIox64.dll/.sys and
 *          run as Administrator.
 *
 * !! WARNING !!
 * This is a low-level tool. While it includes a restore function, misuse can
 * lead to system instability or overheating if thermal limits are ignored.
 * You proceed at your own risk.
 *
 */

#include <Windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <richedit.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

#include "resource.h"
#include "winio.h"
#pragma comment(lib, "WinIox64.lib")

// Window/control IDs
#define ID_EDIT_OUTPUT 101
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_ICON 1
#define ID_TRAY_SHOW 1001
#define ID_TRAY_EXIT 1002

// Global variables
HWND g_hMainWnd = NULL;
HWND g_hEditOutput = NULL;
NOTIFYICONDATA g_nid = {0};
bool g_bMinimizedToTray = false;
bool g_bRunning = true;
HFONT g_hFont = NULL;
WNDPROC g_OrigEditProc = NULL;

// Subclass proc for edit control to forward key input to main window
LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CHAR || msg == WM_KEYDOWN) {
        // Forward to main window
        SendMessage(g_hMainWnd, msg, wParam, lParam);
        return 0;
    }
    return CallWindowProc(g_OrigEditProc, hWnd, msg, wParam, lParam);
}

//==========================The hardware port to read/write function================================
#define READ_PORT(port, data2) GetPortVal(port, &data2, 1);
#define WRITE_PORT(port, data2) SetPortVal(port, data2, 1)
//==================================================================================================

//================================ KBC/PM Channel Functions =================================
#define PM_STATUS_PORT66 0x66
#define PM_CMD_PORT66 0x66
#define PM_DATA_PORT62 0x62
#define PM_OBF 0x01
#define PM_IBF 0x02

void Wait_PM_IBE(void) {
    DWORD data;
    do { READ_PORT(PM_STATUS_PORT66, data); } while (data & PM_IBF);
}

void Wait_PM_OBF(void) {
    DWORD data;
    do { READ_PORT(PM_STATUS_PORT66, data); } while (!(data & PM_OBF));
}

BYTE EC_ReadByte_PM(BYTE index) {
    DWORD data;
    Wait_PM_IBE();
    WRITE_PORT(PM_CMD_PORT66, 0x80);
    Wait_PM_IBE();
    WRITE_PORT(PM_DATA_PORT62, index);
    Wait_PM_OBF();
    READ_PORT(PM_DATA_PORT62, data);
    return (BYTE)data;
}
//=======================================================================================

// --- Low-Level I/O Port Functions ---
UINT8 EC_ADDR_PORT = 0x4E;
UINT8 EC_DATA_PORT = 0x4F;

uint8_t ECRamReadExt(unsigned short address) {
    DWORD data;
    WRITE_PORT(EC_ADDR_PORT, 0x2E);
    WRITE_PORT(EC_DATA_PORT, 0x11);
    WRITE_PORT(EC_ADDR_PORT, 0x2F);
    WRITE_PORT(EC_DATA_PORT, address >> 8);
    WRITE_PORT(EC_ADDR_PORT, 0x2E);
    WRITE_PORT(EC_DATA_PORT, 0x10);
    WRITE_PORT(EC_ADDR_PORT, 0x2F);
    WRITE_PORT(EC_DATA_PORT, address & 0xFF);
    WRITE_PORT(EC_ADDR_PORT, 0x2E);
    WRITE_PORT(EC_DATA_PORT, 0x12);
    WRITE_PORT(EC_ADDR_PORT, 0x2F);
    READ_PORT(EC_DATA_PORT, data);
    return (uint8_t)data;
}

void ECRamWriteExt(unsigned short address, uint8_t value) {
    DWORD data = value;
    WRITE_PORT(EC_ADDR_PORT, 0x2E);
    WRITE_PORT(EC_DATA_PORT, 0x11);
    WRITE_PORT(EC_ADDR_PORT, 0x2F);
    WRITE_PORT(EC_DATA_PORT, address >> 8);
    WRITE_PORT(EC_ADDR_PORT, 0x2E);
    WRITE_PORT(EC_DATA_PORT, 0x10);
    WRITE_PORT(EC_ADDR_PORT, 0x2F);
    WRITE_PORT(EC_DATA_PORT, address & 0xFF);
    WRITE_PORT(EC_ADDR_PORT, 0x2E);
    WRITE_PORT(EC_DATA_PORT, 0x12);
    WRITE_PORT(EC_ADDR_PORT, 0x2F);
    WRITE_PORT(EC_DATA_PORT, data);
}

// --- Fan Curve Structure and Definitions ---
struct FanCurvePoint {
    uint8_t Fan1_RPM; uint8_t Fan2_RPM;
    uint8_t Accel;    uint8_t Decel;
    uint8_t CPU_Max;  uint8_t CPU_Min;
    uint8_t GPU_Max;  uint8_t GPU_Min;
    uint8_t HST_Max;  uint8_t HST_Min;
    uint8_t padding[6];
};

const unsigned short FAN_CURVE_BASE_ADDR = 0xDF00;

const std::vector<FanCurvePoint> defaultCurve = {
    {0, 0, 5, 5, 67, 0, 53, 0, 40, 0},
    {17, 17, 5, 5, 67, 63, 53, 50, 45, 35},
    {19, 19, 5, 5, 67, 63, 53, 50, 50, 40},
    {21, 21, 5, 5, 67, 63, 53, 50, 127, 45},
    {23, 22, 2, 2, 72, 63, 56, 50, 127, 127},
    {25, 27, 2, 2, 77, 67, 59, 53, 127, 127},
    {29, 29, 2, 2, 80, 72, 65, 56, 127, 127},
    {34, 35, 2, 2, 84, 77, 68, 62, 127, 127},
    {37, 37, 2, 2, 88, 80, 75, 65, 127, 127},
    {44, 46, 2, 2, 91, 84, 85, 69, 127, 127},
    {54, 54, 2, 2, 127, 88, 127, 81, 127, 127}
};

std::vector<FanCurvePoint> customCurve;

const std::vector<FanCurvePoint> constant1700Curve = {
    {0, 0, 5, 5, 67, 0, 53, 0, 40, 0},
    {17, 17, 5, 5, 67, 63, 53, 50, 45, 35},
    {17, 17, 5, 5, 67, 63, 53, 50, 50, 40},
    {17, 17, 5, 5, 67, 63, 53, 50, 127, 45},
    {17, 17, 2, 2, 72, 63, 56, 50, 127, 127},
    {17, 17, 2, 2, 77, 67, 59, 53, 127, 127},
    {17, 17, 2, 2, 80, 72, 65, 56, 127, 127},
    {17, 17, 2, 2, 84, 77, 68, 62, 127, 127},
    {17, 17, 2, 2, 88, 80, 75, 65, 127, 127},
    {17, 17, 2, 2, 91, 84, 85, 69, 127, 127},
    {17, 17, 2, 2, 127, 88, 127, 81, 127, 127}
};

const char* INI_FILENAME = "fancurve.ini";

// --- Console Output Function ---
void ConsolePrint(const char* format, ...) {
    char buffer[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Convert \n to \r\n for edit control
    std::string text;
    for (char* p = buffer; *p; p++) {
        if (*p == '\n') {
            text += "\r\n";
        } else {
            text += *p;
        }
    }

    // Append to edit control
    int len = GetWindowTextLengthA(g_hEditOutput);
    SendMessageA(g_hEditOutput, EM_SETSEL, len, len);
    SendMessageA(g_hEditOutput, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());

    // Scroll to bottom
    SendMessageA(g_hEditOutput, EM_SCROLLCARET, 0, 0);
}

void ConsoleClear() {
    SetWindowTextA(g_hEditOutput, "");
}

// --- INI Functions ---
bool LoadCustomCurveFromINI() {
    customCurve.clear();
    FILE* iniFile;
    if (fopen_s(&iniFile, INI_FILENAME, "r") != 0) {
        return false;
    }

    char line[512];
    while (fgets(line, sizeof(line), iniFile)) {
        if (line[0] == ';' || line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }

        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '{') continue;
        p++;

        int values[10] = {0};
        int count = 0;

        while (count < 10 && *p) {
            while (*p == ' ' || *p == '\t') p++;
            int val = 0;
            bool negative = false;
            if (*p == '-') { negative = true; p++; }

            if (*p >= '0' && *p <= '9') {
                while (*p >= '0' && *p <= '9') {
                    val = val * 10 + (*p - '0');
                    p++;
                }
                values[count++] = negative ? -val : val;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == ',') p++;
            } else {
                break;
            }
        }

        if (count == 10) {
            FanCurvePoint point;
            point.Fan1_RPM = (uint8_t)values[0];
            point.Fan2_RPM = (uint8_t)values[1];
            point.Accel    = (uint8_t)values[2];
            point.Decel    = (uint8_t)values[3];
            point.CPU_Max  = (uint8_t)values[4];
            point.CPU_Min  = (uint8_t)values[5];
            point.GPU_Max  = (uint8_t)values[6];
            point.GPU_Min  = (uint8_t)values[7];
            point.HST_Max  = (uint8_t)values[8];
            point.HST_Min  = (uint8_t)values[9];
            customCurve.push_back(point);
        }
    }

    fclose(iniFile);
    return customCurve.size() > 0;
}

void CreateDefaultINI() {
    FILE* iniFile;
    if (fopen_s(&iniFile, INI_FILENAME, "w") != 0) {
        ConsolePrint("ERROR: Could not create %s\n", INI_FILENAME);
        return;
    }

    fprintf(iniFile, "; Legion Fan Curve Configuration\n");
    fprintf(iniFile, "; Format: {Fan1_RPM, Fan2_RPM, Accel, Decel, CPU_Max, CPU_Min, GPU_Max, GPU_Min, HST_Max, HST_Min}\n");
    fprintf(iniFile, "; Fan RPM values are in units of 100 (e.g., 17 = 1700 RPM)\n");
    fprintf(iniFile, "; Temperature values are in Celsius. Use 127 to disable a threshold.\n");
    fprintf(iniFile, "; Set Fan1_RPM and Fan2_RPM to 0 for fans OFF at that level.\n\n");

    const int d[11][10] = {
        {0, 0, 5, 5, 67, 0, 53, 0, 40, 0},
        {17, 17, 5, 5, 67, 63, 53, 50, 45, 35},
        {19, 19, 5, 5, 67, 63, 53, 50, 50, 40},
        {21, 21, 5, 5, 67, 63, 53, 50, 127, 45},
        {23, 22, 2, 2, 72, 63, 56, 50, 127, 127},
        {25, 27, 2, 2, 77, 67, 59, 53, 127, 127},
        {29, 29, 2, 2, 80, 72, 65, 56, 127, 127},
        {34, 35, 2, 2, 84, 77, 68, 62, 127, 127},
        {37, 37, 2, 2, 88, 80, 75, 65, 127, 127},
        {44, 46, 2, 2, 91, 84, 85, 69, 127, 127},
        {54, 54, 2, 2, 127, 88, 127, 81, 127, 127}
    };

    for (int i = 0; i < 11; i++) {
        fprintf(iniFile, "{%d, %d, %d, %d, %d, %d, %d, %d, %d, %d},  // Level %d\n",
            d[i][0], d[i][1], d[i][2], d[i][3], d[i][4],
            d[i][5], d[i][6], d[i][7], d[i][8], d[i][9], i);
    }

    fclose(iniFile);
    ConsolePrint("Created default %s\n", INI_FILENAME);
}

// --- Core Functions ---
void WriteFanCurve(const std::vector<FanCurvePoint>& curve) {
    ConsolePrint("Writing %zu fan curve points to EC memory...\n", curve.size());
    for (size_t i = 0; i < curve.size(); ++i) {
        unsigned short current_addr = FAN_CURVE_BASE_ADDR + (unsigned short)(i * 0x10);
        uint8_t* data = (uint8_t*)&curve[i];
        for (int j = 0; j < sizeof(FanCurvePoint); ++j) {
            ECRamWriteExt(current_addr + j, data[j]);
        }
    }

    ConsolePrint("Setting instant-apply counters...\n");
    ECRamWriteExt(0xC5FE, 0x64);
    ECRamWriteExt(0xC5FF, 0x64);
    ConsolePrint("Write complete.\n");
}

void ReadAndDisplayCurrentCurve() {
    ConsolePrint("\n--- Reading Current Fan Curve from EC ---\n");
    ConsolePrint("LVL | RPM1/2 | Acc/Dec | CPU(Max/Min) | GPU(Max/Min) | HST(Max/Min)\n");
    ConsolePrint("----------------------------------------------------------------------\n");

    for (size_t i = 0; i < defaultCurve.size(); ++i) {
        unsigned short base_addr = FAN_CURVE_BASE_ADDR + (unsigned short)(i * 0x10);

        uint8_t fan1 = ECRamReadExt(base_addr + 0);
        uint8_t fan2 = ECRamReadExt(base_addr + 1);
        uint8_t accl = ECRamReadExt(base_addr + 2);
        uint8_t decl = ECRamReadExt(base_addr + 3);
        uint8_t cmax = ECRamReadExt(base_addr + 4);
        uint8_t cmin = ECRamReadExt(base_addr + 5);
        uint8_t gmax = ECRamReadExt(base_addr + 6);
        uint8_t gmin = ECRamReadExt(base_addr + 7);
        uint8_t hmax = ECRamReadExt(base_addr + 8);
        uint8_t hmin = ECRamReadExt(base_addr + 9);

        ConsolePrint("%-3zu | %-4d/%-4d| %-3d/%-3d | %-4dC/%-4dC  | %-4dC/%-4dC  | %-4dC/%-4dC\n",
            i, fan1 * 100, fan2 * 100, accl, decl, cmax, cmin, gmax, gmin, hmax, hmin);
    }
    ConsolePrint("----------------------------------------------------------------------\n");
}

void DumpECMemory() {
    ConsolePrint("\n--- EC Memory Dumper ---\n");

    FILE* dumpFile;
    if (fopen_s(&dumpFile, "ec_dump.txt", "w") != 0) {
        ConsolePrint("ERROR: Could not create ec_dump.txt.\n");
        return;
    }
    ConsolePrint("Output file 'ec_dump.txt' created.\n");

    ConsolePrint("Reading EC identification...\n");
    fprintf(dumpFile, "--- Embedded Controller Information ---\n");

    uint8_t ec_id1 = ECRamReadExt(0x2000);
    uint8_t ec_id2 = ECRamReadExt(0x2001);
    uint8_t ec_ver = ECRamReadExt(0x2002);

    ConsolePrint("EC Chip ID: ITE-%02X%02X\n", ec_id1, ec_id2);
    ConsolePrint("EC Firmware Version: %u\n", ec_ver);
    fprintf(dumpFile, "EC Chip ID: ITE-%02X%02X\n", ec_id1, ec_id2);
    fprintf(dumpFile, "EC Firmware Version: %u\n", ec_ver);
    fprintf(dumpFile, "--------------------------------------\n\n");

    ConsolePrint("Dumping standard 256-byte EC RAM (0x00-0xFF)...\n");
    fprintf(dumpFile, "--- Standard EC RAM Dump (256 Bytes, via PM Channel) ---\n");
    fprintf(dumpFile, "Offset | 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
    fprintf(dumpFile, "-------|-------------------------------------------------\n");

    for (int i = 0; i <= 0xFF; ++i) {
        if (i % 16 == 0) {
            fprintf(dumpFile, "%02X     | ", i);
        }
        fprintf(dumpFile, "%02X ", EC_ReadByte_PM((BYTE)i));
        if (i % 16 == 15) {
            fprintf(dumpFile, "\n");
        }
    }
    fprintf(dumpFile, "\n\n");

    ConsolePrint("Dumping full 64KB extended EC RAM (0x0000-0xFFFF). This will take a moment...\n");
    fprintf(dumpFile, "--- Full Extended EC RAM Dump (64KB, via Direct I/O) ---\n");
    fprintf(dumpFile, "Address  | 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
    fprintf(dumpFile, "---------|-------------------------------------------------\n");

    for (int i = 0; i <= 0xFFFF; ++i) {
        if (i % 16 == 0) {
            fprintf(dumpFile, "0x%04X   | ", i);
        }
        fprintf(dumpFile, "%02X ", ECRamReadExt((unsigned short)i));
        if (i % 16 == 15) {
            fprintf(dumpFile, "\n");
            if (i % 4096 == 4095) {
                ConsolePrint("Progress: %d%%\n", (i * 100) / 0xFFFF);
            }
        }
    }
    ConsolePrint("Progress: 100%%\n");

    fclose(dumpFile);
    ConsolePrint("\nDump complete. All data saved to ec_dump.txt.\n");
}

// --- System Tray Functions ---
void AddTrayIcon() {
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = g_hMainWnd;
    g_nid.uID = ID_TRAY_ICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON));
    wcscpy_s(g_nid.szTip, L"Legion Fan Control");
    Shell_NotifyIcon(NIM_ADD, &g_nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
}

void MinimizeToTray() {
    ShowWindow(g_hMainWnd, SW_HIDE);
    g_bMinimizedToTray = true;
}

void RestoreFromTray() {
    ShowWindow(g_hMainWnd, SW_SHOW);
    SetForegroundWindow(g_hMainWnd);
    g_bMinimizedToTray = false;
}

// --- Menu Display ---
void ShowMenu() {
    ConsoleClear();
    ConsolePrint("======================================\n");
    ConsolePrint("  Legion EC Fan Curve Writer (ITE-5507)\n");
    ConsolePrint("======================================\n\n");
    ConsolePrint("WARNING: This is a low-level tool. Use responsibly.\n\n");
    ConsolePrint("Select an option:\n");
    ConsolePrint("  [1] Apply CUSTOM Fan Curve (from fancurve.ini)\n");
    ConsolePrint("  [2] Apply CONSTANT 1700 RPM Fan Curve\n");
    ConsolePrint("  [3] Apply CUSTOM CONSTANT RPM (enter your own value)\n");
    ConsolePrint("  [4] Restore DEFAULT Fan Curve\n");
    ConsolePrint("  [5] READ and Display Current Fan Curve from EC\n");
    ConsolePrint("  [6] DUMP Full EC Memory to ec_dump.txt\n");
    ConsolePrint("  [7] Minimize to Tray\n");
    ConsolePrint("  [8] Exit\n\n");
    ConsolePrint("Your choice: ");
}

void HandleMenuChoice(char choice) {
    ConsolePrint("%c\n\n", choice);

    switch (choice) {
    case '1':
        if (LoadCustomCurveFromINI()) {
            ConsolePrint("Loaded %zu levels from %s\n", customCurve.size(), INI_FILENAME);
            ConsolePrint("Applying CUSTOM fan curve...\n");
            WriteFanCurve(customCurve);
            ConsolePrint("\nCUSTOM curve applied successfully!\n");
        } else {
            ConsolePrint("Could not find %s - creating default...\n", INI_FILENAME);
            CreateDefaultINI();
            ConsolePrint("Please edit %s and try again.\n", INI_FILENAME);
        }
        break;
    case '2':
        ConsolePrint("Applying CONSTANT 1700 RPM fan curve...\n");
        WriteFanCurve(constant1700Curve);
        ConsolePrint("\nCONSTANT 1700 RPM curve applied successfully!\n");
        break;
    case '3':
        {
            // For simplicity, use a dialog box for RPM input
            int rpmValue = 1700;  // Default
            // Show input dialog or use default
            ConsolePrint("Using default 1700 RPM (edit code for custom value)...\n");
            uint8_t ecValue = (uint8_t)(rpmValue / 100);

            std::vector<FanCurvePoint> customConstantCurve = {
                {ecValue, ecValue, 5, 5, 67, 0, 53, 0, 40, 0},
                {ecValue, ecValue, 5, 5, 67, 63, 53, 50, 45, 35},
                {ecValue, ecValue, 5, 5, 67, 63, 53, 50, 50, 40},
                {ecValue, ecValue, 5, 5, 67, 63, 53, 50, 127, 45},
                {ecValue, ecValue, 2, 2, 72, 63, 56, 50, 127, 127},
                {ecValue, ecValue, 2, 2, 77, 67, 59, 53, 127, 127},
                {ecValue, ecValue, 2, 2, 80, 72, 65, 56, 127, 127},
                {ecValue, ecValue, 2, 2, 84, 77, 68, 62, 127, 127},
                {ecValue, ecValue, 2, 2, 88, 80, 75, 65, 127, 127},
                {ecValue, ecValue, 2, 2, 91, 84, 85, 69, 127, 127},
                {ecValue, ecValue, 2, 2, 127, 88, 127, 81, 127, 127}
            };

            ConsolePrint("Applying CONSTANT %d RPM (EC value: %d) fan curve...\n", ecValue * 100, ecValue);
            WriteFanCurve(customConstantCurve);
            ConsolePrint("\nCONSTANT %d RPM curve applied successfully!\n", ecValue * 100);
        }
        break;
    case '4':
        ConsolePrint("Applying DEFAULT fan curve...\n");
        WriteFanCurve(defaultCurve);
        ConsolePrint("\nDEFAULT curve restored successfully!\n");
        break;
    case '5':
        ReadAndDisplayCurrentCurve();
        break;
    case '6':
        DumpECMemory();
        break;
    case '7':
        ConsolePrint("Minimizing to system tray...\n");
        ConsolePrint("Click the tray icon to restore, or right-click for menu.\n");
        MinimizeToTray();
        return;  // Don't show "press any key" message
    case '8':
        ConsolePrint("Exiting...\n");
        g_bRunning = false;
        DestroyWindow(g_hMainWnd);
        return;
    default:
        ConsolePrint("Invalid option. Please try again.\n");
        break;
    }

    ConsolePrint("\nPress any key to return to the menu...");
}

// --- Window Procedure ---
bool g_bWaitingForKey = false;

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        {
            // Create monospace edit control for console output
            g_hEditOutput = CreateWindowExA(
                WS_EX_CLIENTEDGE, "EDIT", "",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                0, 0, 100, 100,  // Will be resized in WM_SIZE
                hWnd, (HMENU)ID_EDIT_OUTPUT, GetModuleHandle(NULL), NULL);

            // Subclass the edit control to forward keyboard input
            g_OrigEditProc = (WNDPROC)SetWindowLongPtr(g_hEditOutput, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);

            // Set monospace font
            g_hFont = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
            SendMessage(g_hEditOutput, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            // Set dark background
            // Note: Edit control colors are set in WM_CTLCOLOREDIT
        }
        return 0;

    case WM_SIZE:
        {
            RECT rc;
            GetClientRect(hWnd, &rc);
            MoveWindow(g_hEditOutput, 0, 0, rc.right, rc.bottom, TRUE);
        }
        return 0;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(200, 200, 200));  // Light gray text
            SetBkColor(hdc, RGB(12, 12, 12));       // Dark background
            static HBRUSH hBrush = CreateSolidBrush(RGB(12, 12, 12));
            return (LRESULT)hBrush;
        }

    case WM_CHAR:
        if (g_bWaitingForKey) {
            g_bWaitingForKey = false;
            ShowMenu();
        } else {
            HandleMenuChoice((char)wParam);
            if (wParam != '7' && wParam != '8' && g_bRunning) {
                g_bWaitingForKey = true;
            }
        }
        return 0;

    case WM_TRAYICON:
        switch (lParam) {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            RestoreFromTray();
            break;
        case WM_RBUTTONUP:
            {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenu(hMenu, MF_STRING, ID_TRAY_SHOW, L"Show Window");
                AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");
                SetForegroundWindow(hWnd);
                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
                DestroyMenu(hMenu);
            }
            break;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_SHOW:
            RestoreFromTray();
            break;
        case ID_TRAY_EXIT:
            g_bRunning = false;
            DestroyWindow(g_hMainWnd);
            break;
        }
        return 0;

    case WM_CLOSE:
        // Minimize to tray instead of closing
        MinimizeToTray();
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon();
        if (g_hFont) DeleteObject(g_hFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// --- Entry Point ---
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize WinIO Driver
    if (!InitializeWinIo()) {
        MessageBoxA(NULL,
            "Failed to initialize WinIO driver.\n"
            "Please ensure WinIox64.dll/.sys are present and RUN AS ADMINISTRATOR.",
            "Legion Fan Control - Error", MB_ICONERROR);
        return 1;
    }

    // Register window class
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"LegionFanControlClass";
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));

    if (!RegisterClassEx(&wc)) {
        ShutdownWinIo();
        return 1;
    }

    // Create main window
    g_hMainWnd = CreateWindowEx(
        0, L"LegionFanControlClass", L"Legion Fan Control",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL);

    if (!g_hMainWnd) {
        ShutdownWinIo();
        return 1;
    }

    // Add tray icon
    AddTrayIcon();

    // Show window
    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    // Show initial menu
    ShowMenu();

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    ShutdownWinIo();
    return (int)msg.wParam;
}
