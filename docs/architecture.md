# docs/architecture.md (M0 proposal)

```
Framework Core
  |-- Game Detection (ttmod-detect: PE machine/timestamp/size + FNV; SHA256 externally)
  |-- Engine Profile (profiles/minecraft_story_mode/season{1,2}/)
  |-- Runtime Adapter (per arch: x86 first, x64 later — separate adapters)
  |-- Capability System (hasCapability(); MCSM1 vs MCSM2 subsets differ)
  +-- Common Mod API (versioned; adapters only, no raw internals)
```

Layers: `core/` (detect, sigmatch, profile, log, shared `init_from_exe`) → `loader/windows/` (clean-room dinput8 proxy + framework DLL, M1) → `runtime/` (hooks/memory/signatures/events/api, M2+) → `profiles/` → `formats/` → `scripts/` → `mods/` → `tools/` → `tests/` → `docs/`. No GUI in core.

Key decisions: never hard-code MCSM1 addresses (signatures + validation + per-build profiles); capabilities gate every feature; offline tooling builds on Linux, runtime DLL needs Windows x86 toolchain (VS2022 x86 / mingw-w64 — not installed here, so `TTMOD_BUILD_WIN_RUNTIME` stays OFF on Linux and the DLL sources are _WIN32-guarded).
