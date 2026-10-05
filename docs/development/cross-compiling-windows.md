# Cross-compiling the Windows x86 runtime on Linux

The win32 runtime builds from Linux with the system MinGW-w64 toolchain —
the same one CI installs. No Windows machine needed.

## Required packages

- Arch/CachyOS: `sudo pacman -S mingw-w64-gcc` (provides the full
  `i686-w64-mingw32` triplet: gcc, g++, crt, headers, binutils)
- Debian/Ubuntu: `sudo apt-get install mingw-w64 binutils` (`objdump`, used by
  `verify_win32.sh`, is the `binutils` package — the runner has it preinstalled
  but naming it keeps the step honest)
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

## CI

`.github/workflows/ci.yml` runs four jobs on `ubuntu-24.04`: native
debug + tests, this cross-build, ASan and UBSan (separate jobs, not one —
a sanitizer failure should not hide the other's result), and the changed-line
format gate. The cross-build job uploads `dinput8.dll` +
`ttmod_framework.dll` as the `win32-dlls` artifact, so a verified game DLL
can be pulled from a green run instead of rebuilt by hand.

Two checks exist because they each caught a real bug:

- **Embedded Lua markers.** `menumods_ui.lua` is embedded at build time, so
  an edit that never reaches the binary looks exactly like a no-op edit.
  The job greps the built DLL for markers from the current `.lua`.
- **Format gate on an unresolvable base.** `tools/check_format.py` diffs
  changed lines against a base rev. If that rev does not exist (force-push,
  first push to a new branch) it falls back to checking the whole tree
  instead of reporting zero files and passing.

Locally, the equivalent of the format gate is
`python3 tools/check_format.py HEAD~1`; the default argument is
`merge-base(HEAD, origin/master)`. Note the gate is clang-format
version-sensitive — CI prints the resolved version in its log.

## Build output
