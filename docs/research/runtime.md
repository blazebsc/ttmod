# docs/research/runtime.md

## Status
- Known: MCSM1 is x86 PE32; proxy-DLL injection is plausible (telltale_hook ships `dinput8.dll`+`telltale_hook.dll` next to exe, incl. `MINECRAFTSTORYMODE` build). No hook written yet.
- Observed: Lua 5.2.3 strings + `\x1bLEo` encrypted scripts; FMOD + VERSION imports; no `luaL_*` exports in strings (statically linked / renamed — needs disassembly).
- Hypothesis: fastest M2 probe = proxy DLL that logs load + tries Lua exec (telltale_hook path), else hook a low-risk import (Version API) first.
- Unknown: real function addresses/signatures, calling conventions, thread layout, resource-lookup path (VFS vs archive priority vs resdesc).

## Per-subsystem log
Use `Status/Known/Observed/Hypothesis/Unknown/Evidence/Confidence/Next test` per subsystem. No subsystem graduates to API without a runtime test.

## MCSM1 → verified-hook route (smallest)
1. `ttmod-detect` (done, offline) → 2. proxy DLL bootstrap + log → 3. one validated signature hook → 4. plugin `hello_mcsm` → 5. visible mod (text/Lua/property).
