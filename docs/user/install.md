# Install + use TTMod (normal users)

## 1. Requirements
- A legitimate Minecraft: Story Mode Season 1 PC install (Steam-era build).
  Only the builds in `docs/games/mcsm1-support.md` are tested - other builds
  safely idle with a log message instead of loading mods.
- Windows (Wine 8+ works on Linux; native Windows untested by this team yet -
  please report results).

## 2. Install the framework (once)
Copy these two files next to `MinecraftStoryMode.exe`:

```text
dinput8.dll
ttmod_framework.dll
```

That is the whole installation. Nothing is patched; remove the two DLLs to
uninstall the framework itself.

## 3. First launch
Launch the game normally. TTMod creates `mods/`, `config/`, `logs/` and the
internal `ttmod/` directory, writes `logs/ttmod.log`, and the game starts as
usual. Check the first lines of the log:

```text
TTMod framework v0.13.0 starting
Profile: mcsm1_pc_x86 status=supported
[TTMod] Valid mods: 0 ...
```

## 4. Install a mod
Drop a `.zip` file (or an unpacked mod folder with `manifest.json`) into
`mods/` and launch the game. No commands needed.

```text
Minecraft Story Mode/mods/
├── SomeMod.zip           # packaged: just drop it in
└── DevMod/              # unpacked: manifest.json + files/ + plugins/
```

## 5. Enable / disable
**In-game** (recommended): launch the game → main menu → **Mods**.
It lists every installed mod with version and [ON]/[OFF]; click a mod for
details, toggle Enabled, or edit its settings. Changes apply after a game
restart. A mod the loader blocked (conflict, missing dependency) shows a
`Blocked: <reason>` row on its details screen instead of failing silently.

**By file**: to disable without deleting, create/edit
`config/mods.json`:

```json
{
    "SomeMod": {"enabled": false}
}
```

Delete the file entry (or the whole file) to re-enable to manifest default.

## 6. Turn the in-game menu off
Set env `TTMOD_MENU=0`, or create an empty file `config/menu-disabled`.
The start menu stays pixel-stock; mods still load and work (only the button
is gone). Delete the file / unset the env to bring it back.

## 7. Remove a mod
Delete the `.zip` (or folder) from `mods/`. Next launch it's gone; stale
cache under `ttmod/cache/` is cleaned automatically.

## 8. If a mod breaks the game
All diagnostics live in `logs/ttmod.log` (plus `logs/ttmod.exit.log` with a
one-line session summary). Start there.

| Symptom | Likely cause → next step |
|---|---|
| Game doesn't start at all (no window, instant exit) | First check `logs/` exists: if no log, the framework never loaded → DLLs misplaced (see §2). If a log ends mid-bootstrap, note the last line and report it. Try safe mode to rule out mods |
| No `mods/` after launch | Framework DLLs not beside the exe, or wrong `dinput8.dll` won DLL search → verify the two files sit next to `MinecraftStoryMode.exe`; check a `logs/` dir appeared |
| `Skipped: X: invalid manifest …` | Open the mod's `manifest.json`, fix the named field |
| `Skipped: X: unsupported API …` | Mod needs a newer TTMod (log header shows `TTMod framework v…` - update framework) |
| `Skipped: X: game not supported` | Mod targets another game/season; nothing to fix on MCSM1 |
| `missing dependency …` | Install the named mod into `mods/` too |
| `conflicts with present mod` | Keep the winner (higher priority, logged) or remove one |
| `rejected (arch … != x86)` | 64-bit plugin on 32-bit MCSM1; needs an x86 build of the mod |
| `declared plugin missing` | Broken package; re-download or unpack manually to inspect |
| `invalid package: …` | Corrupt/hostile ZIP (traversal, dup names, no manifest); re-download |
| Game white-screens/crashes | Disable suspects via `config/mods.json`, then safe mode below |
| Still broken? Safe mode | Set env `TTMOD_SAFE_MODE=1`, or create an empty file `config/safe-mode`, then launch - all third-party mods stay inert. Delete the file (or unset the env) to leave safe mode |
| How to verify TTMod loaded | `logs/ttmod.log` starts with `TTMod framework v…` + `Profile: mcsm1_pc_x86 status=supported` |

## 9. Native-code warning
A mod with a DLL logs `WARNING ... can execute arbitrary code` on load.
Only use native mods you trust — the trust model is in `SECURITY.md`
(root). Resource-only mods (no DLL) only replace data files the game
reads.

## 10. Uninstall everything
1. Delete `dinput8.dll` + `ttmod_framework.dll` (framework gone).
2. Optionally delete `mods/`, `config/`, `logs/`, `ttmod/` (your mods/state).
3. Verify the game directory contains only original files again.
4. The game was never patched: archives/exe are byte-identical (check MD5 if
   you like).
