# docs/research/telltale_hook.md - reference analysis (no code copied)

Source: https://github.com/HW12Dev/telltale_hook @ main (2 commits), cloned 2026-09-16 to /tmp only.
License: **no LICENSE file → treat as all-rights-reserved. Reference only.**

## Bootstrap
- `proxy/src/main.cpp`: `dinput8.dll` proxy. On ATTACH loads `%SystemRoot%\System32\dinput8.dll`,
  resolves `DirectInput8Create`, `LoadLibrary("telltale_hook.dll")`, forwards the one export.
- Viable for MCSM1: exe imports `DINPUT8.dll` (verified via objdump import table).
- Weaknesses: `MessageBox` + `ExitProcess(0)` if system DLL missing (hostile to the game);
  no path pinning (DLL search-order risk); frees both DLLs on DETACH without hook teardown.

## Framework DLL
- `src/main.cpp` DllMain spawns a thread running `setup()` (correct: leaves loader lock).
- `setup()`: `AllocConsole` + `freopen CONOUT$`, deletes `log.txt`, MinHook init,
  populates Lua/engine pointers as `GetModuleHandle(exe)+hardcoded RVA`, hooks
  `lua_newstate` + `ScriptManager::LoadResource`, registers `tthookprint`, replaces
  global `print`, then **infinite `while(true)` F1/F2 poll that never exits** (leaked thread).
- `logging.cpp`: `log()/logln()` append to `log.txt` + stdout; no levels, no rotation, no thread sync.
- `config.hpp` declares JSON config but `config.cpp` is a 3-line stub - no config system exists.

## Hook model
- MinHook (submodule) inline hooks; `HOOK_FUNCTION` macro logs create/enable per function.
- `lua.hpp` is the per-game table: `#ifdef GAME_*` selects x86 vs x64 signatures and RVAs.
- MCSM1 (`GAME_MINECRAFTSTORYMODE`, x86): lua_newstate 0x611C80, pcallk 0x60D3B0,
  pushcclosure 0x60C850, setglobal 0x60CDD0, tolstring 0x60C310, loadfilex 0x60E8F0,
  loadstring 0x60EBF0, pushboolean 0x60C8F0, gettop 0x60B860,
  ScriptManager__LoadResource 0x1139F0, CRC32 0x24A670, CRC64_CI 0x24A620,
  TTArchive2__Activate 0x6003D0.
- `telltale_types.hpp` warns its own `TTArchive2` struct **"is incorrect for MCSMS1"** - do not reuse blindly.

## Build / compat
- CMake, C++20, `set(TELLTALE_GAME SMSTW_REMASTERED)` compile-time default; VS2022 x86
  (amd64 for 64-bit games). Game identity is **compile-time**, not detected - our
  profile system must replace this. ImGui backend present but commented out.

## What we adopt (concepts only)
dinput8-proxy bootstrap shape; LoadResource+newstate as first-hook candidates;
per-game offset tables evolving into signature+profile tables.
## What we reject
Hardcoded RVAs as primary mechanism; compile-time game identity; console alloc;
infinite poll thread; ExitProcess/MessageBox failure mode; unstructured log.txt.
