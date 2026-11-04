# XeUnshackle Compressed is a fork of a XeUnshackle without any GUI and graphics using

## Source files
- `stdafx.h` — XDK types + all declarations
- `stdafx.cpp` — one line include (can ignore)
- `XeUnshackle.cpp` — **all** logic in one file with section comments

## Sections inside XeUnshackle.cpp (for those who want to edit XeUnshackle.cpp)
1. Locale
2. Utils (UART, mounts, files, XNotify, keys/MAC)
3. HVFuncs
4. FreebootPatches
5. Dashlaunch
6. main()

This project was made by me (compiler) and Grok (code)

## How to build it and run it
1.1. Download any `XDKSetupXenon*.exe` file (for example it will be XDKSEtupXenon*.exe file)
1.2. Download Visual Studio 2022 installer (not VSCode!)
1.3. Download `RXDK-360.exe` installer
2.1. Setup `XDKSetupXenon*.exe` file 
2.2. Setup Visual Studio 2022 
2.3. Setup RXDK-360
3.1. In Visual Studio 2022, create a Xbox 360 Title project, and copy all files from repository to project (or just clone this repository using Git)
3.2. Compile it using modern method with Clang builder and CL compiler (in `XDKSetupXenon*.exe` file)
3.3. If build was succesful (it compiled but not copied/copied to Xbox 360 console), use `xextool.exe` and run `xextool.exe -m r -r a -o default.xex XeUnshackle.xex`
