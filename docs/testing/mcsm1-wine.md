# MCSM1 Wine validation (M1 bootstrap)

## Environment

- Wine 11.17, default prefix `~/.wine` (`#arch=win64`, WoW64: `system32`+`syswow64`)
- No Lutris; Steam present but unused. Game launched directly: `wine MinecraftStoryMode.exe`
- Game: `Minecraft - Story Mode/MinecraftStoryMode.exe`
  size 12179904, timestamp 1463779093,
  SHA256 `88443673…27817f7` (see `docs/games/mcsm1.md`)
- Loader: `build-win32/dinput8.dll` (PE32/i386, 25930 B) +
  `build-win32/ttmod_framework.dll` (PE32/i386, 362546 B)

## Procedure

1. `sh tools/verify_win32.sh build-win32` → ALL CHECKS PASSED
2. Copy both DLLs beside the exe (no original files touched).
3. `timeout 60 wine MinecraftStoryMode.exe` from the game dir.
4. Inspect `ttmod.log`; remove DLLs + log afterwards.

## Result: PASS (2026-09-16, 3 runs)

`ttmod.log`:

```text
[INFO] TTMod framework starting
[INFO] Detected executable: H:\Documents\games\mcsm_decom\Minecraft - Story Mode\MinecraftStoryMode.exe
[INFO] Architecture: x86
[INFO] Game: minecraft-story-mode
[INFO] Season: 1
[INFO] Profile: mcsm1_pc_x86 status=supported
[INFO] Framework initialization complete (M1: no hooks, game unmodified)
[INFO] exe_base=00400000 image_size=0x00BF6000 (read from headers, not assumed)
```

- Framework loads, profile correct, log written. ✅
- No crash, no loader error; wine output shows normal d3d9 init + GLSL
  shader compiles (game reaches rendering init with proxy in place). ✅
- Baseline control (DLLs removed): identical `exit=0` after ~4 s, only fd
  numbers differ → proxy does not perturb startup/shutdown. ✅
- Game dir restored pristine (DLLs + `ttmod.log` removed; only
  game-created `3DMGAME/Player` save dir remains).

## Wine-specific observations

- DLL search: app-dir `dinput8.dll` wins over Wine builtin with no
  `WINEDLLOVERRIDES` needed. Proxy forwards to real `dinput8` via
  `GetSystemDirectoryA` (resolves to `syswow64` under WoW64) - game input
  path intact (startup identical to baseline).
- Paths: `GetModuleFileNameA` returns `H:\...` Unix-mapped path; our logger
  and `parse_pe` handle it (detection succeeded on the Wine path).
- Image base under Wine: `0x00400000` == link-time ImageBase
  (`SizeOfImage 0xBF6000` matches objdump) - no ASLR slide observed here,
  but the framework reads headers at runtime and never assumes it.
- Threads: `CreateThread` from `DllMain` (after `DisableThreadLibraryCalls`)
  works under Wine; init runs off the loader lock.
- No Wine-specific hacks added; same binaries should work on native Windows
  (not yet tested - needs a Windows x86 machine for final confirmation).
- Unexplored: 4 s `exit=0` is baseline behavior in this headless-ish
  environment (DISPLAY=:0 present); not investigated since identical
  with/without framework.

## Known problems

- None blocking M1. `MESA-EGL`/d3d `fixme`s are environment noise.
