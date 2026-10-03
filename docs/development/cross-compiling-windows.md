# Cross-compiling the Windows x86 runtime on Linux

The win32 runtime builds from Linux with the system MinGW-w64 toolchain —
the same one CI installs. No Windows machine needed.

## Required packages

- Arch/CachyOS: `sudo pacman -S mingw-w64-gcc` (provides the full
  `i686-w64-mingw32` triplet: gcc, g++, crt, headers, binutils)
- Debian/Ubuntu: `sudo apt-get install mingw-w64`
- Host `cmake` / `g++` / `python3` / `lua5.1` / `lua5.2` stay for the Linux
  build and tests (CI installs exactly this set).

## Compiler triplet

`i686-w64-mingw32-gcc` / `i686-w64-mingw32-g++`:

```sh
i686-w64-mingw32-g++ --version
```

## CMake commands

```sh
# Linux native (default, runtime OFF)
cmake -S . -B build && cmake --build build && ctest --test-dir build

# Windows x86 cross (separate dir - never mix object files)
cmake -S . -B build-win32 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake \
  -DTTMOD_BUILD_WIN_RUNTIME=ON && cmake --build build-win32

# Verify artifacts (PE arch + exports + system-only imports)
sh tools/verify_win32.sh build-win32
```

`cmake/mingw-i686.cmake` finds the triplet on PATH and fatals with the
install command if absent. Runtime DLLs link `-static-libgcc
-static-libstdc++ -static` so no MinGW DLLs ship beside the game, and
`dinput8` links `-Wl,--kill-at` so the export is undecorated
`DirectInput8Create` (stdcall `@20` decoration would break game imports).

**CRT note:** toolchains differ in which system C runtime they link.
Debian/Ubuntu mingw (what CI uses for release artifacts) links classic
`msvcrt.dll` - present on every Windows since 95. Arch/CachyOS mingw links
the UCRT (`api-ms-win-crt-*`) - present on Windows 10+ and in any modern
Wine, but Windows 7/8 need the UCRT redist. Both pass `verify_win32.sh`;
build with the Debian toolchain if you must support Win7/8.

## Build output
