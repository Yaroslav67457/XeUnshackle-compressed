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
1. Download any `XDKSetupXenon*.exe` file (for example it will be `XDKSetupXenon*.exe` file)
2. Download Visual Studio 2022 installer (not VSCode!)
3. Download `RXDK-360.exe` installer
4. Setup `XDKSetupXenon*.exe` file 
5. Setup Visual Studio 2022 
6. Setup RXDK-360
7. In Visual Studio 2022, create a Xbox 360 Title project, and copy all files from repository to project (or just clone this repository using Git)
8. Compile it using modern method with Clang builder and CL compiler (in `XDKSetupXenon*.exe` file)
9. If build was succesful (it compiled but not copied/copied to Xbox 360 console), use `xextool.exe` and run `xextool.exe -m r -r a -o default.xex XeUnshackle.xex`
