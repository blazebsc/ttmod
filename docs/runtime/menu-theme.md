# menu-theme: menu text recolour via the engine's own prop system (MCSM1)

**Status:** WORKING (proven in the real game, live log evidence). One gap is
engine-owned and cannot be fixed without a native detour (see below).

## What it does

A mod named `menu.theme` (shipped in `mods/menu-theme/` of the game dir) reads
`config/menu.theme.json` for an accent colour and tints every menu widget the
engine creates, for both our Mods menu AND the game's own menus. Put any
`#RRGGBB` in the config; relaunch to see it.

```json
{"accent": "#FF8000", "menus": "all"}
```

`"menus": "ttmod"` limits the tint to the framework's own Mods screens.
`"menus": "all"` (default) hits every menu in the game.

There's also an in-game colour palette on the mod's details row: eight swatches
per page, three pages, `Next >>` / `<< Previous`, `*` marks the current one,
and a Back row returns to details.

## How to check it works

Launch, open Mods, and read the log:

```sh
cd "Minecraft - Story Mode"
awk '/TTMod framework v/{buf=""} {buf=buf $0 "\n"} END{printf "%s",buf}' logs/ttmod.log \
  | grep -E "menu-theme|paint|verify|Selection Color"
```

You want to see:

- `menu-theme: accent '#XXXXXX' from config/menu.theme.json (ui chunk queued)` — the mod loaded and queued its accent
- `verify-ok: Text Color holds (exact) on label` — the write landed
- `sweep-root-all: ... | Selection Color | Text Color | ...` — the aug labels in the agent tree

If `Plugins: menu.theme validated` and `initialized (rc=0)` are missing, the
mod didn't load; check `mods/menu-theme/manifest.json` declares
`"plugin": "plugins/menu-theme.dll"`.

## What we proved about the engine

- **Runtime Lua is 5.1, and `_G` is NIL.** `setmetatable`, `table.unpack`, and
  `_G` are all missing from the stripped environment. Any `_G.Foo` throws, and
  inside a click callback that's a silent kill instead of a catch. Bare globals
  only; since the framework's Lua runs on the ENGINE's state, menu code must
  live as bare globals to survive.
- **`Text Color` is the label property.** It reads back as a `{r, g, b, a}`
  table of 0..1 floats — and accepts the same shape on write. Strings and
  0..255 ints are silently ignored.
- **The widget tree is TWO agents deep on rows:** `widget.agent ->
  ui_listButton_button clone -> .agent -> label`. The root agent holds
  `Selection Color`; the button clone also has its own (`Text Color` on both,
  plus `Button - Command`).
- **The property registry lives in the exe (unpacked dump shows the faithful
  version, not the on-disk packed exe).** Registered objects sit at
  `0xDD56C0-0xDD56E8` (`mov ecx, 0xdd56c0 + call 0x43a660`).
- **Ghidra work:** the unpacked image is dumpable (`TTMOD_DUMP_MEM=<path>` env,
  writes once when Menu.lua loads). We used it to trace the registration
  thunks and the readers of the descriptor objects — proving the render loop
  never reads a property on hover. Details below.

## The known limitation: hover goes white and stays white (engine-owned)

**Working:** the text flashes white while you hover the row, and accent returns
when the menu repaints.
**Engine-side:** nothing restores our colour after the mouse leaves. The render
loop reads a stock stylesheet, not a property we control.

This is built into the engine, not a settable Lua property:

- The property system we're using registers "Text Color", "Selection Color",
  and trigger callbacks in ONE function (FUN_006d7790); there's no "restore on
  unhover" path. Our accent lands when the widget is created. The engine reads
  its own render path every frame.
- The hover flash is rendered, not data-driven. Sweeps and probes for a
  'Highlight Color', 'Focus Color', or similar found nothing to set. The
  Trigger callback properties exist but are 3D scene triggers — they never
  fired on a UI widget in any session.
- Two other things were tested and ruled out: painting the ListButton prototype's
  `ListButton.agent` (nil), and blanking `Button - Chore Select/Deselect/Press`
  (no effect — the flash isn't chore-driven).

*If you want this fixed, the route is a native detour.* The register thunk at
0x43A660 and the functions that read the descriptor objects are the seam, but
they're inside the engine's per-frame render — an engine patch, not a mod.
Every other claim about fixing white-on-hover is a claim about something else.

## How it was built

It's proxy-DLL injection (`dinput8.dll` chain-loads `ttmod_framework.dll`),
hooking late after the game unpacks, then Lua-side detours at the game's own
function points. The build is MinGW-w64 i686 → Windows DLL (see
`docs/development/cross-compiling-windows.md`); the runtime dump is optional
(`TTMOD_DUMP_MEM=<path>` writes an unpacked image once, at Menu.lua load).

Property names are engine-registered by name, not hashed: "Text Color" is one of
~40 strings registered in the image. We scan for our target on the cloned agent
via the engine's `AgentGetProperties()`, `AgentGetProperty()`, `Clone_Find()`,
and `AgentSetProperty()` entry points (all at known RVAs in this build), walk
the widget tree (`widget.agent -> ui_listButton_button clone -> label`), and
write the colour once per session on the first widget the engine creates. No
per-frame polling.

The rule this addon works by: **we only set what's provably settable.** If the
engine doesn't accept the write (still reads its stock value after our set), we
say so in the log rather than hide it. The hover white-out is the one place the
engine's own choice wins, and we say that on the tin.
