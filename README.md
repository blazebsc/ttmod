# ttmod - Telltale Mod Framework (Minecraft: Story Mode first)

## Normal use (no CLI, no GUI)

1. Install TTMod Framework once (copy `dinput8.dll` + `ttmod_framework.dll`
   beside `MinecraftStoryMode.exe`).
2. Launch MCSM once - the framework creates `mods/`, `config/`, `logs/`, `ttmod/`.
3. Download a mod (`.ttmod` file).
4. Drag it into `mods/`:
   ```text
   Minecraft Story Mode/
   ├── mods/
   │   ├── BetterDialogue.ttmod
   │   └── MyTestMod/
   │       └── manifest.json
   ├── config/      ← framework/user state (mods.json)
   ├── logs/        ← ttmod.log
   └── ttmod/       ← framework-owned cache (do not touch)
   ```
5. Launch the game. The mod loads automatically.
6. Delete the `.ttmod` to uninstall. Disable via `config/mods.json`
   (`{"MyMod": {"enabled": false}}`) without deleting anything.

You may also place an unpacked mod directory containing `manifest.json`
directly into `mods/` for development/testing - same system, no repackaging.

Mod format, discovery, state, and security: `docs/runtime/mod-packages.md`,
`docs/runtime/mod-discovery.md`.

## Developer builds

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
./build/ttmod detect "/path/MinecraftStoryMode.exe"
./build/ttmod package create mymod/ mymod.ttmod
```

Windows cross-build + Wine validation: `docs/development/cross-compiling-windows.md`,
`docs/testing/mcsm1-wine.md`.

## Status

M1 bootstrap · M2 hooks/research · M3 plugins · M4 first mod · M5 resource
overrides · M6 Lua research · M7 events · M8 game state · M9 multi-build ·
M10 deps · M11 drop-in packages · in-game Mods menu (main-menu button →
list/details/toggles, proven live 2026-10-01). Details per milestone under
`docs/`.
License: MIT (see LICENSE); third-party terms: `docs/third-party.md`.
