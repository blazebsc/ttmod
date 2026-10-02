# Troubleshooting

All diagnostics live in `logs/ttmod.log` (plus `logs/ttmod.exit.log` with a
one-line session summary). Start there.

| Symptom | Likely cause → next step |
|---|---|
| Game doesn't start at all (no window, instant exit) | First check `logs/` exists: if no log, the framework never loaded → DLLs misplaced (see Install). If a log ends mid-bootstrap, note the last line and report it. Try safe mode to rule out mods |
| No `mods/` after launch | Framework DLLs not beside the exe, or wrong `dinput8.dll` won DLL search → verify the two files sit next to `MinecraftStoryMode.exe`; check a `logs/` dir appeared |
| `Skipped: X: invalid manifest …` | Open the mod's `manifest.json`, fix the named field |
| `rejected (api …)` | Mod needs a newer TTMod (`Detected: TTMod API …` is in the log header — update framework) |
| `rejected (game not supported)` | Mod targets another game/season; nothing to fix on MCSM1 |
| `missing dependency …` | Install the named mod into `mods/` too |
| `conflicts with present mod` | Keep the winner (higher priority, logged) or remove one |
| `rejected (arch … != x86)` | 64-bit plugin on 32-bit MCSM1; needs an x86 build of the mod |
| `declared plugin missing` | Broken package; re-download or unpack manually to inspect |
| `invalid package: …` | Corrupt/hostile ZIP (traversal, dup names, no manifest); re-download |
| Game white-screens/crashes | Disable suspects via `config/mods.json`, then safe mode (`config/safe-mode` empty file or `TTMOD_SAFE_MODE=1`) |
| Game occasionally runs long | Observed rarely under Wine in test (1 in ~40 runs); kill and relaunch — baseline game shows the same variance without TTMod installed |
| How to verify TTMod loaded | `logs/ttmod.log` starts with `TTMod framework v…` + `Profile: mcsm1_pc_x86 status=supported` |
| Back to vanilla | Delete the two framework DLLs; optionally remove `mods/ config/ logs/ ttmod/`; verify with MD5 that game files are untouched |
