# Text Recolour (mod id `menu.theme`): menu text recolour via the engine's own prop system (MCSM1)

**Status:** WORKING. Proven in-game with screenshots: every menu label renders
the configured accent at rest and on hover, dialogs included; disabled rows
keep their dimmed look; arrows and buttons unaffected.

## What it is

A mod (`mods/menu-theme/`, display name "Text Recolour") that tints every menu
widget the engine creates. It reads `config/menu.theme.json`:

```json
{"accent": "#FF8000", "menus": "all"}
```

- `accent`: any `#RRGGBB`. Applied at launch (config changes need restart).
- `menus`: `"all"` (default) themes every menu; `"ttmod"` limits the tint to
  the framework's own Mods screens.

The Mods menu exposes both keys as config rows (colour palette + scope enum),
so the colour can be changed in-game; it applies on restart.

## How it works (two layers)

**Layer 1 — Lua paint** (`loader/windows/menu/menumods_ui.lua`): the `Menu_Add`
wrapper routes every created widget through `TTMOD_THEME_WIDGET`, which writes
the accent (0..1 floats, named `{r,g,b,a}` fields — the engine's actual
representation; 0..255 ints get clamped and render stock) into `Text Color`
on each label clone and `Selection Color` on the button clone. A retry queue
covers widgets returned before their label child exists. Verified in-game
(`verify-ok: Text Color holds (exact)`).

**Layer 2 — native substitute** (`loader/windows/lua/lua_bridge.cpp`, `hook_scol`
on the engine color setter at RVA `0x168430`, thiscall, anchor-verified,
retry-installed): near-gray-bright color structs are rewritten to the accent
in place. This catches engine-side writes that bypass Lua entirely (select
white, stock restores). Gated on a valid accent in config *and* the mod being
enabled in `config/mods.json`; black fills, disabled gray, and real tints pass
through. Kill-switch: `TTMOD_SETCOLOR=0`.

**The in-place write is load-bearing — do not "fix" it to a copy.** The
engine's mouse-off "stay" path reads its own persistent color struct
directly, bypassing this setter entirely. Both failure modes were proven
in-game, in both directions: substituting via a stack copy leaves the
engine's struct stock-white, so the hovered row turns WHITE after mouse-off;
substituting in place puts the accent into the struct, which is the only
thing the bypass path ever reads.

**The global default-colour register (2026-10-09, model closed).** The
static struct at `kScolLightStructRva` (0x9893E4, stock RGBA 1,1,1,1) is
the engine's GLOBAL DEFAULT-COLOUR REGISTER. Three consumers, all proven:

- **Material creation** copies it into every new material's light
  (`movq [reg+0x58]` from the struct, found at many sites). Render is
  multiplicative: `Text Color x Light`. The theme IS this register,
  recoloured to the accent - every menu material created afterwards
  renders accent-tinted, at rest and on hover alike.
- **The styling writes** (the two `caller=002980E3/0029811B` flood sites)
  push this register as the colour they write ('Light Color Diffuse'/
  'Specular', plus scalar material state via the second setter).
- **The unhover restore** reads it as the colour a deselected row reverts
  to - one global, by design, on every screen. This is the original bug:
  the register was poisoned accent, so unhovered rows stuck accent until
  restart. Blacking it turned the whole menu black (multiplication);
  whiting it made restores stick white.

**Six pick rows per page + markup swatch labels (2026-10-11, shipped).** The
suppression build proved its own negative: zero white scol writes arrived
during the entire picker visit (suppress counter stayed 0), yet rows still
turned white - the unhover restore bypasses scol 100% (scalar/direct
path), and no rollover event of any kind fires in Lua. Per-row MATERIAL
colour is closed conclusively (see the proof chain below). The palette now
ships as 16 swatches, 6 per page (3 pages) of one pick button each, plus
Next/Previous and Back - 8 rows, the proven-rendered capacity. The gate,
the white register, and the substitution skip stay as defense in depth
(zero cost when no traffic flows).

**Per-row TEXT colour via the engine's own markup (2026-10-11).** The
game's own UI scripts - decrypted from `MCSM_pc_Menu_data.ttarch2` (70
scripts incl. `Menu.lua`, `Menu_Stats.lua`, `WidgetInitializer.lua`) -
set NO widget colour property anywhere: colours are authored scene data
applied by the engine's material styler, never Lua-driven, which is the
final nail in the per-row-material coffin. But they DO colour menu text
dynamically with inline markup: `Menu_Stats.lua` sets

```lua
AgentSetProperty('ui_stats_statDescription', 'Text String',
  '^font:pexico_large^^glyphScale:.80^^color:#ffff99^' .. title .. '^^ \n' .. desc)
```

The text renderer parses `^color:#rrggbb^ ... ^^` inside `Text String` -
a render-layer feature entirely separate from the material path, so it
survives every hover/unhover material rebuild (the string is never
rewritten). The picker uses the game's exact idiom: each swatch row's
hex label renders IN that swatch's colour
(`setlabel(r, '^color:' .. hex:lower() .. '^' .. hex .. mark .. '^^', true)`).
The label is passed raw - the game bypasses `EscapeText2` for markup
strings, and escaping would eat the `^` tags.

**Markup rendering CONFIRMED in-game (2026-10-11, user session).** Every
picker label rendered a DIFFERENT shade - per-row colour works - but all
shades were GREEN: the glyph render is `markup colour x material light`,
and the material light is the global register, which the 10-11 build
still held at the (green) accent during the picker. Hover changed
nothing (the string drives glyphs, material rebuilds never touch it) -
exactly the multiplication model the decompile predicted.

**White register during the picker (2026-10-11, second build).** The
fix for the green tint: `lua_bridge_set_light_palette` now HONORS its
`white_phase` argument - gate open writes WHITE (identity) to the
register (`kScolLightStructRva`), gate close restores the accent. White
is what the STOCK game runs (stock register is 1,1,1,1), so the picker
chrome renders stock while markup labels show TRUE colours.
`hook_scol` skips the near-white->accent substitution for the same
window: the rebuild's white Light Color writes must stay white, or the
substitution would re-tint chrome and glyphs back to the accent family
(the exact reported bug). On close, the picker's materials die with its
screen and the accent returns everywhere else. The old "white proved
destructive" note was pre-gate (ungated whiting stuck restores white
menu-wide); the gate + skip contains the white window to the picker
visit. **CONFIRMED IN-GAME (user session, 2026-10-11): "the colour
picker is now working nice"** - true-colour swatch labels on the
white-register picker. The 11-path impossibility saga closes here:
per-row material colour remains impossible, per-row TEXT colour is
shipped and verified.

**Reveal-repopulate (2026-10-11, log-proven).** The engine RE-RUNS a
revealed screen's `menu.Populate` when a pop exposes it (log:
`populate: color grid done` lines with no matching `color: pick`).
Consequences, both handled: the picker's gate-open moved INSIDE
`Populate` (a revealed picker re-opens the gate and keeps true colours),
and screen replacement must mind that revealed screens rebuild
themselves with whatever `Populate` reads.

**Menu-stack: the lock-yield model and Menu_Replace (2026-10-11, fourth
build).** Back needing 3-4 presses and overshooting to the main menu had
TWO rounds of fixes - the first (pop-before-push in the page command)
was wrong medicine, still broken in the follow-up session. Reading the
game's own `Menu.lua` (decrypted) closed it:

- **`Menu_Pop` yields on lock.** `Menu_Pop` (game Menu.lua) starts with
  `while Menu_StackMemberLocked() do Yield() end`; the lock is an OR of
  four flags, including the transition flag `Menu_Show` holds for its
  whole duration and the click's own press/roll state. A click command
  of the form `'Menu_Pop();<builder>()'` therefore suspends mid-command
  and its pop completes LATE - interleaving with the push and the next
  click across frames (the log showed pushes before pops and pages
  reappearing). The game's own scripts only ever pop+pop or pop+rebuild
  (`'Menu_Pop();Menu_Hide();Menu_Main()'`); for sibling screens they use
  a dedicated primitive.
- **`Menu_Replace(menu, existing)` is that primitive** (Menu.lua): with
  `existing == currentMenu` it degrades to a pure `Menu_Show` - the new
  screen replaces the old IN PLACE, return stack untouched, no pop in
  the command at all. (With `existing` deeper in the stack it drains
  down to and INCLUDING it - the replaced screen is discarded as a
  Back target.)
- **Shipped design**: page clicks are plain
  `'Menu_Mods_PickColor(...)'`; inside, if `Menu_GetCurrentMenu()` is one
  of our picker pages (tagged `menu.ttmod_page`), the new page goes in
  via `Menu_Replace` - paging never touches the stack. First open keeps
  `Menu_Push` (details stays the Back target). Back is one pure pop.
- `Menu_Mods_SetColor` (a pick) pops twice (picker, then the stale
  details) before `Menu_Mods_Select` pushes the fresh screen - stack
  `[main, list, details-fresh]`. (`Toggle`/`EditString` correctly
  single-pop: they run ON the details screen itself; `Select` never
  pops - details must nest over the list.)

**Per-row material impossibility - final proof chain (2026-10-11).**
Every path closed by direct in-game or decompiled evidence:

- `Text Color` (agent property): stored but NOT rendered - all-orange
  pages while it held exact swatch values (verify lines proved both).
- `Light Color Diffuse` (the decompiled render property, written by
  `FUN_00697a60` state 0): unreachable from Lua - `AgentGetProperty`
  reads MISSING on every agent; the material is an internal object at
  widget+0x20 with no Lua path (Mesh/Material `Clone_Find` children:
  "not present").
- The global default-colour register (`0x9893E4`): one colour for ALL
  widgets, by design.
- The material rebuild (`FUN_00698310`, all 14 states) rewrites every
  widget's Light Color from the register on every hover/unhover.
- Lua hover events: never fire (theme-roll count 0 across sessions).
- The game's own scripts set no colour property on any widget (see the
  decryption pipeline in `in-game-mod-menu.md` - the same idiom
  space our menu uses).

## Proven behaviour (screenshot-verified)

- Rest and hovered rows render the accent (green/blue/white exact).
- Hover lightens slightly toward white and *stays* on the last-touched row
  after mouse-off. That is the engine's selection model (no mouse-off
  deselect exists), not a bug in the theme: stock behaves the same way, it is
  just invisible when everything is white-on-white.
- Dialogue/subtitle whites also theme while the mod is enabled (same setter).
- Orange `#FF8000` renders reddish at rest and exact on hover: the rest path
  misreads one channel order that symmetric hues (white/green/blue) never
  expose. Cosmetic; greens/blues are pixel-perfect. A `#0080FF` probe renders
  blue exactly at rest (no channel swap in the data path).
- No per-frame render hook exists: the earlier flag-flatten attempt
  (`0x56F820` arg) disarmed click dispatch engine-wide and was reverted the
  same day. The render loop is observed, never patched.

## How to check it works

Launch, open Mods, and read the log:

```sh
cd "Minecraft - Story Mode"
awk '/TTMod framework v/{buf=""} {buf=buf $0 "\n"} END{printf "%s",buf}' logs/ttmod.log \
  | grep -E "menu-theme|verify-ok|scol-sub|theme-audit"
```

You want to see:

- `menu-theme: accent '#XXXXXX' from config/menu.theme.json (ui chunk queued)`
- `verify-ok: Text Color holds (exact) on label`
- `scol-sub: 1.000,1.000,1.000 -> accent` (engine whites intercepted)
- No `theme-audit:` lines (nothing standing overwritten; audits skip
  not-yet-painted labels so mid-build reads never false-flag)

If `menu.theme validated` / `initialized (rc=0)` are missing, the mod didn't
load; check `mods/menu-theme/manifest.json` declares
`"plugin": "plugins/menu-theme.dll"`.

## Engine findings (durable, from the unpacked image + REA/Ghidra)

- Runtime Lua is 5.1 with `_G` nil; `setmetatable`, `table.unpack` missing.
  Chunk top-level must only DEFINE, never CALL (libs open after state
  capture). Declaration order is load-bearing; see
  `docs/runtime/in-game-mod-menu.md`.
- `Text Color` is the label property: `{r,g,b,a}` 0..1 floats. `Selection
  Color` lives on the button clone (hover fill). `Button - Command` carries
  the click DoString (errors swallowed - debug via `ttmod_menu_log`).
- Widget tree is two agents deep: `widget.agent -> ui_listButton_button`
  clone `-> .agent -> label`. `Clone_Find` wants `.agent`, throws otherwise.
- Lua registry (all validated in-image): `AgentSetProperty` binding
  `0x3842A0`; `TextSetColor` `0x730690`; `RolloverEnableTextColor` `0x73F730`
  (dead in menu flow - region never executes); `Rollover*Callback`
  `0x73F260`/`0x73F420`; native setter `0x568430` (thiscall, the hooked one);
  `0x5684B0` is a property GETTER (substituting there was a no-op);
  render dispatch `FUN_005746a0` draws highlight via `0x56F820(800, flag,
  ...)` per-frame from `GameEngine::Loop`; rollover status global
  `0xDD8CA8`; class default Text Color datum is pure white.
- Hover/press state has no settable property (324-name sweep + full binding
  enumeration + native traffic analysis). Trigger Entered/Exited callbacks
  fired zero times on UI widgets (scene triggers only) and were removed.
- Menu screens rebuild on revisit (fresh widgets); the main menu never pops,
  so its agents stay valid while submenu agents go stale after pop.

## How it was built

Proxy-DLL injection (`dinput8.dll` chain-loads `ttmod_framework.dll`),
late anchor-verified hooks only (init-time detours hang Wine), Lua detours at
the game's own function points, MinGW-w64 i686 build
(`docs/development/cross-compiling-windows.md`). Demanding-mod policy
(`docs/runtime/api-policy.md`): every hook exists because this mod needed it.
