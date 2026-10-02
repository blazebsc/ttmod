# Cross-compiling the Windows x86 runtime on Linux

No sudo, no Windows machine. Toolchain comes from nixpkgs (binary cache).

## Required packages

Nothing installed system-wide. All from nix (`nix-shell`):
- `pkgsCross.mingw32` GCC 15.2.0 + binutils 2.44 (mingw-w64 CRT/headers 13.0.0)
- Host `cmake` 4.4.3 / `g++` 16.2.1 stay for the Linux build.

If nix is unavailable: Arch/CachyOS `mingw-w64-gcc`, `mingw-w64-crt`,
`mingw-w64-headers`, `mingw-w64-binutils` provide the same
`i686-w64-mingw32-{gcc,g++}` triplet (needs root - not used here).

## Compiler triplet

`i686-w64-mingw32-gcc` / `i686-w64-mingw32-g++`, via `nix/mingw-shell.nix`:

```sh
nix-shell nix/mingw-shell.nix --run 'i686-w64-mingw32-g++ --version'
```

## CMake commands

```sh
# Linux native (default, runtime OFF)
cmake -S . -B build && cmake --build build && ctest --test-dir build

# Windows x86 cross (separate dir - never mix object files)
nix-shell nix/mingw-shell.nix --run \
  'cmake -S . -B build-win32 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake \
   -DTTMOD_BUILD_WIN_RUNTIME=ON && cmake --build build-win32'

# Verify artifacts (PE arch + exports + system-only imports)
sh tools/verify_win32.sh build-win32
```

`cmake/mingw-i686.cmake` finds the triplet on PATH and fatals with the
nix-shell command if absent. Runtime DLLs link `-static-libgcc
-static-libstdc++ -static` so no MinGW DLLs ship beside the game, and
`dinput8` links `-Wl,--kill-at` so the export is undecorated
`DirectInput8Create` (stdcall `@20` decoration would break game imports).

## Build output

- `build-win32/dinput8.dll` - PE32/i386 proxy, ~26 KB
- `build-win32/ttmod_framework.dll` - PE32/i386 framework, ~360 KB
- (Stale `lib*.dll` names from before the PREFIX fix are deleted, not shipped.)

## Wine test procedure

See `docs/testing/mcsm1-wine.md`. Short form: copy both DLLs beside
`MinecraftStoryMode.exe`, `wine MinecraftStoryMode.exe`, expect `ttmod.log`
with `Profile: mcsm1_pc_x86 status=supported`, then remove the DLLs + log.

## Known limitations

- `windres`/resource files: none used, no .rc needed.
- Debug info: `-g` PDB-style; stripped not required for M1.
- The cross `ttmod-detect.exe`/test `.exe`s also build but are not run
  (PE); unit tests run from the Linux build.
