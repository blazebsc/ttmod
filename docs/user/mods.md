# Mods (normal users)

## Where mods go
```text
Minecraft Story Mode/mods/
├── SomeMod.ttmod        # packaged: just drop it in
└── DevMod/              # unpacked: manifest.json + files/ + plugins/
```

## Enable / disable

**In-game** (recommended): launch the game → main menu → **Mods**
(between Settings and Exit Game). It lists every installed mod with version
and [ON]/[OFF]; click a mod for details, toggle Enabled, or edit its
settings. Changes apply after a game restart.

**By file**: to disable without deleting, create/edit
`config/mods.json`:

**Turn the in-game menu off**: set env `TTMOD_MENU=0`, or create an empty
file `config/menu-disabled`. The start menu stays pixel-stock; mods still
load and work (only the button is gone). Delete the file / unset the env
to bring it back.

```json
{
    "SomeMod": {"enabled": false}
}
```

Delete the file entry (or the whole file) to re-enable to manifest default.

## Remove
Delete the `.ttmod` (or folder) from `mods/`. Next launch it's gone; stale
cache under `ttmod/cache/` is cleaned automatically.

## If a mod breaks the game
1. Read `logs/ttmod.log` - search `Skipped:`, `rejected`, `WARNING`.
2. Disable the suspect mod in `config/mods.json`, or delete it.
3. Still broken? Safe mode: set env `TTMOD_SAFE_MODE=1`, or create an empty
   file `config/safe-mode`, then launch - all third-party mods stay inert.
4. Delete `config/safe-mode` (or unset the env var) to leave safe mode.

## Native-code warning
A mod with a DLL logs `WARNING ... can execute arbitrary code` on load.
Only use native mods you trust. Resource-only mods (no DLL) cannot execute
code through TTMod (they only replace data files the game reads).
