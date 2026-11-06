# XeUnshackle Compressed

Headless fork of [XeUnshackle](https://github.com/Byrom90/XeUnshackle) for use with the [Xbox360BadUpdate](https://github.com/grimdoomer/Xbox360BadUpdate) exploit.

No GUI, no intro video, no fonts. The app applies HV/Kernel patches, loads Dashlaunch, optionally runs BadStorage, saves console info, then exits to the dashboard.

> **This is not a permanent softmod.**  
> After power off or reboot the console returns to an unmodified state.  
> **Backup your NAND first.** Do not flash modified NAND images.

## Features

- Full freeboot-style HV and Kernel patches (same idea as upstream XeUnshackle)
- Dashlaunch (`launch.xex`) loaded from embedded sections
- BadStorage support if `GAME:\BadStorage.xex.dll` is present
- Saves `GAME:\ConsoleInfo.txt` (CPUKey, DVDKey, motherboard type)
- Optional backup of the original MAC address
- Single main source file — easy to read and edit

## Source layout

| File | Role |
|------|------|
| `stdafx.h` | XDK types and declarations |
| `stdafx.cpp` | One-line include |
| `XeUnshackle.cpp` | All application logic in one file |

### Sections inside `XeUnshackle.cpp`

1. Locale  
2. Utils (UART, mounts, files, XNotify, keys/MAC)  
3. HVFuncs  
4. FreebootPatches  
5. Dashlaunch  
6. `main()`

## Requirements

- Xbox 360 XDK (recommended: [XDK 21256.3 on Archive.org](https://archive.org/details/xbox-360-sdk-21256.3_202204))
- Visual Studio 2022
- [RXDK-360](https://github.com/Team-Resurgent/RXDK-360) (or another working Xbox 360 toolchain for VS 2022)

## Build

1. Install the Xbox 360 XDK.
2. Install Visual Studio 2022.
3. Install RXDK-360 and point it at your XDK.
4. Open `XeUnshackle.vcxproj` in Visual Studio  
   (or create an Xbox 360 Title project and copy these files into it).
5. Build configuration: **Release_LTCG | Xbox 360**.
6. Convert the raw XEX with xextool:

```bat
xextool.exe -m r -r a -o default.xex XeUnshackle.xex
```

- `-m r` — retail media flags  
- `-r a` — all regions  
- `default.xex` — file you deploy to the console  

**Important:** do not copy the raw `XeUnshackle.xex` to the console. Always run xextool first. Using the raw file often causes "Unable to start game".

## Usage

1. Place `default.xex` in your BadUpdate payload folder (USB or internal drive).
2. Run the BadUpdate exploit as usual.
3. The app applies patches and returns to the dashboard.
4. Optional: open `GAME:\ConsoleInfo.txt` to read CPUKey / DVDKey / board type.

## Credits

- [Byrom90 / XeUnshackle](https://github.com/Byrom90/XeUnshackle) — original project
- [grimdoomer](https://github.com/grimdoomer/Xbox360BadUpdate) — Xbox360BadUpdate exploit
- cOz, InvoxiPlayGames, and everyone credited in upstream XeUnshackle
- Compressed / headless build: me + Grok (AI)

## Disclaimer

Released as-is. You are responsible for what you load and for any damage to your console or data. Use at your own risk.
