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
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <conio.h> // For _getch()

#include "winio.h"
#pragma comment(lib, "WinIox64.lib")

//==========================The hardware port to read/write function================================
#define READ_PORT(port, data2) GetPortVal(port, &data2, 1);
#define WRITE_PORT(port, data2) SetPortVal(port, data2, 1)
//==================================================================================================

//================================ KBC/PM Channel Functions =================================
// Using PM Channel (62/66) for standard EC RAM access
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
    WRITE_PORT(PM_CMD_PORT66, 0x80); // Read command
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

// Protocol to read from extended EC RAM (addresses > 0xFF)
uint8_t ECRamReadExt(unsigned short address) {
    DWORD data;
    WRITE_PORT(EC_ADDR_PORT, 0x2E);
    WRITE_PORT(EC_DATA_PORT, 0x11);
    WRITE_PORT(EC_ADDR_PORT, 0x2F);
    WRITE_PORT(EC_DATA_PORT, address >> 8); // High byte
    WRITE_PORT(EC_ADDR_PORT, 0x2E);
    WRITE_PORT(EC_DATA_PORT, 0x10);
    WRITE_PORT(EC_ADDR_PORT, 0x2F);
    WRITE_PORT(EC_DATA_PORT, address & 0xFF); // Low byte
    WRITE_PORT(EC_ADDR_PORT, 0x2E);
    WRITE_PORT(EC_DATA_PORT, 0x12);
    WRITE_PORT(EC_ADDR_PORT, 0x2F);
    READ_PORT(EC_DATA_PORT, data);
    return (uint8_t)data;
}

// Protocol to write to extended EC RAM (addresses > 0xFF)
void ECRamWriteExt(unsigned short address, uint8_t value) {
    DWORD data = value;
    WRITE_PORT(EC_ADDR_PORT, 0x2E);
    WRITE_PORT(EC_DATA_PORT, 0x11);
    WRITE_PORT(EC_ADDR_PORT, 0x2F);
    WRITE_PORT(EC_DATA_PORT, address >> 8); // High byte
    WRITE_PORT(EC_ADDR_PORT, 0x2E);
    WRITE_PORT(EC_DATA_PORT, 0x10);
    WRITE_PORT(EC_ADDR_PORT, 0x2F);
    WRITE_PORT(EC_DATA_PORT, address & 0xFF); // Low byte
    WRITE_PORT(EC_ADDR_PORT, 0x2E);
    WRITE_PORT(EC_DATA_PORT, 0x12);
    WRITE_PORT(EC_ADDR_PORT, 0x2F);
    WRITE_PORT(EC_DATA_PORT, data);
}

// --- Fan Curve Structure and Definitions ---

// Structure to hold one point of the fan curve
struct FanCurvePoint {
    uint8_t Fan1_RPM; uint8_t Fan2_RPM;
    uint8_t Accel;    uint8_t Decel;
    uint8_t CPU_Max;  uint8_t CPU_Min;
    uint8_t GPU_Max;  uint8_t GPU_Min;
    uint8_t HST_Max;  uint8_t HST_Min;
    // The EC table has 6 empty bytes at the end of each 16-byte entry
    uint8_t padding[6];
};

// Base address for the fan curve table in your EC dump
const unsigned short FAN_CURVE_BASE_ADDR = 0xDF00;

// Hardcoded DEFAULT fan curve (discovered from EC dump)
const std::vector<FanCurvePoint> defaultCurve = {
    {0, 0, 5, 5, 67, 0, 53, 0, 40, 0},               // Level 0 (from 0xDF00)
    {17, 17, 5, 5, 67, 63, 53, 50, 45, 35},          // Level 1 (from 0xDF10)
    {19, 19, 5, 5, 67, 63, 53, 50, 50, 40},          // Level 2 (from 0xDF20)
    {21, 21, 5, 5, 67, 63, 53, 50, 127, 45},         // Level 3 (from 0xDF30)
    {23, 22, 2, 2, 72, 63, 56, 50, 127, 127},        // Level 4 (from 0xDF40)
    {25, 27, 2, 2, 77, 67, 59, 53, 127, 127},        // Level 5 (from 0xDF50)
    {29, 29, 2, 2, 80, 72, 65, 56, 127, 127},        // Level 6 (from 0xDF60)
    {34, 35, 2, 2, 84, 77, 68, 62, 127, 127},        // Level 7 (from 0xDF70)
    {37, 37, 2, 2, 88, 80, 75, 65, 127, 127},        // Level 8 (from 0xDF80)
    {44, 46, 2, 2, 91, 84, 85, 69, 127, 127},        // Level 9 (from 0xDF90)
    {54, 54, 2, 2, 127, 88, 127, 81, 127, 127}       // Level 10 (from 0xDFA0)
};

// Custom fan curve (loaded from fancurve.ini)
std::vector<FanCurvePoint> customCurve;

// Constant 1700 RPM fan curve (17 = 1700 RPM in EC units)
const std::vector<FanCurvePoint> constant1700Curve = {
    {0, 0, 5, 5, 67, 0, 53, 0, 40, 0},               // Level 0
    {17, 17, 5, 5, 67, 63, 53, 50, 45, 35},            // Level 1
    {17, 17, 5, 5, 67, 63, 53, 50, 50, 40},            // Level 2
    {17, 17, 5, 5, 67, 63, 53, 50, 127, 45},           // Level 3
    {17, 17, 2, 2, 72, 63, 56, 50, 127, 127},          // Level 4
    {17, 17, 2, 2, 77, 67, 59, 53, 127, 127},          // Level 5
    {17, 17, 2, 2, 80, 72, 65, 56, 127, 127},          // Level 6
    {17, 17, 2, 2, 84, 77, 68, 62, 127, 127},          // Level 7
    {17, 17, 2, 2, 88, 80, 75, 65, 127, 127},          // Level 8
    {17, 17, 2, 2, 91, 84, 85, 69, 127, 127},          // Level 9
    {17, 17, 2, 2, 127, 88, 127, 81, 127, 127}         // Level 10
};

const char* INI_FILENAME = "fancurve.ini";

// Loads custom fan curve from INI file
// Format: {Fan1_RPM, Fan2_RPM, Accel, Decel, CPU_Max, CPU_Min, GPU_Max, GPU_Min, HST_Max, HST_Min}, // comment
// Returns true if successful, false if file not found or error
bool LoadCustomCurveFromINI() {
    customCurve.clear();

    FILE* iniFile;
    if (fopen_s(&iniFile, INI_FILENAME, "r") != 0) {
        return false;
    }

    char line[512];
    while (fgets(line, sizeof(line), iniFile)) {
        // Skip comments and empty lines
        if (line[0] == ';' || line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }

        // Look for lines starting with '{'
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;  // Skip leading whitespace

        if (*p != '{') continue;
        p++;  // Skip '{'

        // Parse 10 comma-separated values
        int values[10] = {0};
        int count = 0;

        while (count < 10 && *p) {
            // Skip whitespace
            while (*p == ' ' || *p == '\t') p++;

            // Parse number
            int val = 0;
            bool negative = false;
            if (*p == '-') { negative = true; p++; }

            if (*p >= '0' && *p <= '9') {
                while (*p >= '0' && *p <= '9') {
                    val = val * 10 + (*p - '0');
                    p++;
                }
                values[count++] = negative ? -val : val;

                // Skip to next value (comma or closing brace)
                while (*p == ' ' || *p == '\t') p++;
                if (*p == ',') p++;
            } else {
                break;  // Invalid character
            }
        }

        // If we got all 10 values, add this level
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

// Creates a default INI file with the current custom curve values
void CreateDefaultINI() {
    FILE* iniFile;
    if (fopen_s(&iniFile, INI_FILENAME, "w") != 0) {
        printf("ERROR: Could not create %s\n", INI_FILENAME);
        return;
    }

    fprintf(iniFile, "; Legion Fan Curve Configuration\n");
    fprintf(iniFile, "; Format: {Fan1_RPM, Fan2_RPM, Accel, Decel, CPU_Max, CPU_Min, GPU_Max, GPU_Min, HST_Max, HST_Min}\n");
    fprintf(iniFile, "; Fan RPM values are in units of 100 (e.g., 17 = 1700 RPM)\n");
    fprintf(iniFile, "; Temperature values are in Celsius. Use 127 to disable a threshold.\n");
    fprintf(iniFile, "; Set Fan1_RPM and Fan2_RPM to 0 for fans OFF at that level.\n\n");

    // Write default silent curve values in C++ array format
    const int d[11][10] = {
        {0, 0, 5, 5, 67, 0, 53, 0, 40, 0},
        {0, 0, 5, 5, 67, 63, 53, 50, 45, 35},
        {0, 0, 5, 5, 67, 63, 53, 50, 50, 40},
        {0, 0, 5, 5, 67, 63, 53, 50, 127, 45},
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
    printf("Created default %s\n", INI_FILENAME);
}

// --- Core Functions ---

// Writes a full fan curve to the EC
void WriteFanCurve(const std::vector<FanCurvePoint>& curve) {
    printf("Writing %zu fan curve points to EC memory...\n", curve.size());
    for (size_t i = 0; i < curve.size(); ++i) {
        unsigned short current_addr = FAN_CURVE_BASE_ADDR + (unsigned short)(i * 0x10);
        uint8_t* data = (uint8_t*)&curve[i];

        // Write the 16 bytes for this curve point
        for (int j = 0; j < sizeof(FanCurvePoint); ++j) {
            ECRamWriteExt(current_addr + j, data[j]);
        }
    }

    // Tell the EC to apply the changes instantly
    printf("Setting instant-apply counters...\n");
    ECRamWriteExt(0xC5FE, 0x64); // 100 in decimal
    ECRamWriteExt(0xC5FF, 0x64); // 100 in decimal

    printf("Write complete.\n");
}

// Reads the current fan curve from the EC and displays it
void ReadAndDisplayCurrentCurve() {
    printf("\n--- Reading Current Fan Curve from EC ---\n");
    printf("LVL | RPM1/2 | Acc/Dec | CPU(Max/Min) | GPU(Max/Min) | HST(Max/Min)\n");
    printf("----------------------------------------------------------------------\n");

    for (int i = 0; i < defaultCurve.size(); ++i) {
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

        printf("%-3d | %-4d/%-4d| %-3d/%-3d | %-4dC/%-4dC  | %-4dC/%-4dC  | %-4dC/%-4dC\n",
            i, fan1 * 100, fan2 * 100, accl, decl, cmax, cmin, gmax, gmin, hmax, hmin);
    }
    printf("----------------------------------------------------------------------\n");
}

// Dumps the full EC memory to ec_dump.txt
void DumpECMemory() {
    printf("\n--- EC Memory Dumper ---\n");

    // Open the output file
    FILE* dumpFile;
    if (fopen_s(&dumpFile, "ec_dump.txt", "w") != 0) {
        printf("ERROR: Could not create ec_dump.txt.\n");
        return;
    }
    printf("Output file 'ec_dump.txt' created.\n");

    // --- Step 1: Identify the Embedded Controller ---
    printf("Reading EC identification...\n");
    fprintf(dumpFile, "--- Embedded Controller Information ---\n");

    uint8_t ec_id1 = ECRamReadExt(0x2000);
    uint8_t ec_id2 = ECRamReadExt(0x2001);
    uint8_t ec_ver = ECRamReadExt(0x2002);

    printf("EC Chip ID: ITE-%02X%02X\n", ec_id1, ec_id2);
    printf("EC Firmware Version: %u\n", ec_ver);
    fprintf(dumpFile, "EC Chip ID: ITE-%02X%02X\n", ec_id1, ec_id2);
    fprintf(dumpFile, "EC Firmware Version: %u\n", ec_ver);
    fprintf(dumpFile, "--------------------------------------\n\n");

    // --- Step 2: Dump the Standard 256-byte EC RAM (PM Channel) ---
    printf("Dumping standard 256-byte EC RAM (0x00-0xFF)...\n");
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

    // --- Step 3: Dump the Full 64KB Extended EC RAM (Direct I/O) ---
    printf("Dumping full 64KB extended EC RAM (0x0000-0xFFFF). This will take a moment...\n");
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
            // Print progress to console
            if (i % 256 == 255) {
                printf("\rProgress: %d%%", (i * 100) / 0xFFFF);
            }
        }
    }
    printf("\rProgress: 100%%\n");

    // --- Cleanup ---
    fclose(dumpFile);
    printf("\nDump complete. All data saved to ec_dump.txt.\n");
}


// --- Main Program Loop ---
int main() {
    // Initialize WinIO Driver
    if (!InitializeWinIo()) {
        printf("FATAL: Failed to initialize WinIO driver.\n");
        printf("Please ensure WinIox64.dll/.sys are present and RUN AS ADMINISTRATOR.\n");
        _getch();
        return 1;
    }

    // Main menu loop
    while (true) {
        system("cls"); // Clear the screen
        printf("======================================\n");
        printf("  Legion EC Fan Curve Writer (ITE-5507)\n");
        printf("======================================\n\n");
        printf("WARNING: This is a low-level tool. Use responsibly.\n\n");
        printf("Select an option:\n");
        printf("  [1] Apply CUSTOM Fan Curve (from fancurve.ini)\n");
        printf("  [2] Apply CONSTANT 1700 RPM Fan Curve\n");
        printf("  [3] Restore DEFAULT Fan Curve\n");
        printf("  [4] READ and Display Current Fan Curve from EC\n");
        printf("  [5] DUMP Full EC Memory to ec_dump.txt\n");
        printf("  [6] Exit\n\n");
        printf("Your choice: ");

        char choice = _getch();
        printf("%c\n\n", choice);

        switch (choice) {
        case '1':
            if (LoadCustomCurveFromINI()) {
                printf("Loaded %zu levels from %s\n", customCurve.size(), INI_FILENAME);
                printf("Applying CUSTOM fan curve...\n");
                WriteFanCurve(customCurve);
                printf("\nCUSTOM curve applied successfully!\n");
            } else {
                printf("Could not find %s - creating default...\n", INI_FILENAME);
                CreateDefaultINI();
                printf("Please edit %s and try again.\n", INI_FILENAME);
            }
            break;
        case '2':
            printf("Applying CONSTANT 1700 RPM fan curve...\n");
            WriteFanCurve(constant1700Curve);
            printf("\nCONSTANT 1700 RPM curve applied successfully!\n");
            break;
        case '3':
            printf("Applying DEFAULT fan curve...\n");
            WriteFanCurve(defaultCurve);
            printf("\nDEFAULT curve restored successfully!\n");
            break;
        case '4':
            ReadAndDisplayCurrentCurve();
            break;
        case '5':
            DumpECMemory();
            break;
        case '6':
            printf("Exiting...\n");
            ShutdownWinIo();
            return 0;
        default:
            printf("Invalid option. Please try again.\n");
            break;
        }

        printf("\nPress any key to return to the menu...");
        _getch();
    }

    // Cleanup
    ShutdownWinIo();
    return 0;
}