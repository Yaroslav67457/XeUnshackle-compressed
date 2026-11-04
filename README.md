# XeUnshackle Ultra-minimal (2–3 source files)

## Source files
- `stdafx.h` — XDK types + all declarations
- `stdafx.cpp` — one line include (can ignore)
- `XeUnshackle.cpp` — **all** logic in one file with section comments

## Sections inside XeUnshackle.cpp
1. Locale
2. Utils (UART, mounts, files, XNotify, keys/MAC)
3. HVFuncs
4. FreebootPatches
5. Dashlaunch
6. main()

No GUI, video, audio, ATG.
