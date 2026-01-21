# LegionFanControl NEW

This is a dirty viby steamy FORK of https://github.com/0x1F9F1/LegionFanControl

```text
╔══════════════════════════════════════════════════════════════╗
║                                                              ║
║                   !!!   DISCLAIMER   !!!                     ║
║                                                              ║
╠══════════════════════════════════════════════════════════════╣
║                                                              ║
║   This program grants raw access to your                     ║
║   laptop's Embedded Controller (EC).                         ║
║                                                              ║
║   You are solely responsible for any damage, permanent or    ║
║   otherwise, that may result from its use. You have been     ║
║   warned.                                                    ║
║                                                              ║
╚══════════════════════════════════════════════════════════════╝
```
NEITHER I NOR GEMINI ASSUME ANY RESPONSIBILITY WHATSOEVER

This program allows you to tweak the fans of Legion Pro 5 16IRX8 (SO IT IS GENERATION 8 YEAR 2023). Maybe it works on similar laptops.

You can either put your own values in the .ini(for instance disabling the fans completely(not recommended AT ALL)), apply constant lowest level of fans at every temperature(1700RPM, that's what I use it for), restore defaults and other stuff:

```text
Select an option:
  [1] Apply CUSTOM Fan Curve (from fancurve.ini)
  [2] Apply CONSTANT 1700 RPM Fan Curve
  [3] Apply CUSTOM CONSTANT RPM (enter your own value)
  [4] Restore DEFAULT Fan Curve
  [5] READ and Display Current Fan Curve from EC
  [6] DUMP Full EC Memory to ec_dump.txt
  [7] Minimize to Tray
  [8] Exit
```
All thanks to the previous author, to SmokelessCPUv2, Zandyz, and Underv0lti, and to Gemini, guided by my sage wisdom.

//The whole program is a simple, single .cpp file so feel free to inspect/build it yourself

You may need to disable Core Isolation/Memory Integrity and change this registry key 
VulnerableDriverBlocklistEnable to 0 
at
HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\CI\Config

the default original curve in the firmware:
```code
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
    {54, 54, 2, 2, 127, 88, 127, 81, 127, 127}       // Level 10 (from 0xDFA0)};  
    ↑    ↑          ↑ 
    ↑    ↑          ↑ 
    the values in the first two columns are the speed of the fans at certain temperatures, the values starting from the fifth column are the temperatures
```

