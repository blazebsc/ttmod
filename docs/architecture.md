# docs/architecture.md (M0 proposal)

```
Framework Core
  |-- Game Detection (ttmod-detect: PE machine/timestamp/size + FNV; SHA256 externally)
  |-- Engine Profile (profiles/minecraft_story_mode/season{1,2}/)
  |-- Runtime Adapter (per arch: x86 first, x64 later - separate adapters)
  |-- Capability System (per-profile feature gates; MCSM1 vs MCSM2 subsets differ)
  +-- Common Mod API (versioned; adapters only, no raw internals)
```

Layers: `core/` (detect, sigmatch, profile, log, discovery, resolver, shared `init_from_exe`) → `loader/windows/` (clean-room dinput8 proxy + framework DLL: hooks, lua_bridge, menu_bridge, mods, plugins, events) → `profiles/` (per-game/per-season) → `third_party/` (minhook, miniz, imgui) → `tools/` (offline CLI, Wine matrix, release) → `tests/` → `examples/` → `docs/`. No GUI in core.

Key decisions: never hard-code MCSM1 addresses (signatures + validation + per-build profiles); profiles gate every feature; offline tooling builds on Linux, runtime DLL cross-compiles to Windows x86 via the system MinGW-w64 toolchain (`mingw-w64-gcc`, `TTMOD_BUILD_WIN_RUNTIME=ON`; _WIN32-guarded sources).
