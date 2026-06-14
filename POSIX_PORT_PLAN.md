# POSIX/Linux Port Plan

This repository now has the first portability layer in `src/Platform.h/.cpp`.

Remaining work before Linux is fully supported:

1. Move the Winsock server loop in `src/main.cpp` behind a small `platform::HttpServer` wrapper.
2. Move `Win32LineReader` out of `DataReader.cpp` and provide:
   - Windows implementation based on `ReadFile`.
   - POSIX implementation based on `open`, `read`, `lseek`, `close`.
3. Keep the `.dat` binary cache format fixed-width and versioned. Version 2 is now written by `Halo::saveToBinary`.
4. Build commands:
   - Windows: `mingw32-make windows` or the README `g++` command.
   - Linux target is scaffold-only until socket and line-reader split are complete.
