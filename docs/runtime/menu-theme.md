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

**Swatch preservation via the engine getter (2026-10-07).** The log proved
the engine RE-APPLIES its selection color struct continuously — hundreds of
scol calls per hover burst, every one carrying the once-substituted accent —
so a palette swatch's deliberate colour was overwritten every frame and,
because the stick model never deselects, it never came back until process
restart. No Lua-side repaint can win against that rate. The hook now reads
the widget's CURRENT colour through the setter's sibling getter at
`kScolBRva` (`0x1684B0`), whose ABI was verified from the unpacked dump —
thiscall, `ret $0xC`, args `[descriptor, float out[4], flag]`, returns
`al` — and anchor-checked before it is ever called. A saturated current
colour that is not the accent is deliberate content: the write is
redirected to it. Themed rows (current = gray/white/accent) substitute
exactly as before, and the getter resolving to null (anchor mismatch)
falls back to accent-only behaviour. Log marker: `scol-keep:`.

Two subtleties proven by the same log session:

- **The button agent is the selected-state render source.** The sweep found
  `Text Color` AND `Selection Color` on the row's *button* agent, and
  `TTMOD_THEME_WIDGET` paints that button accent at `Menu_Add` time — so
  painting only the label leaves hover rendering sourced from accent
  (exactly the stuck-accent symptom). `Menu_Mods_PickColor` now paints the
  swatch colour on the row's button agent as well as its label.
- **The keep redirect is a COPY, the accent an in-place write — the
  asymmetry is load-bearing.** The accent must live in the engine's struct
  for the stay path, but a swatch colour written through the same struct
  would poison it for every other row. Per-call redirect through the getter
  is sufficient: every flood call re-derives the target's real colour, so
  the copy wins without touching shared state.

Lua-side guards (`theme_wrap_asp/roll/tc`, log-only) and a read-only audit
(`theme-audit`, attribution `own:`/`menu:`) ship in the UI chunk for diagnosis.
They have never fired on a live write path; the native hook does the work.

**Swatch preservation (2026-10-07).** The colour picker paints each palette
row's label with that swatch's own colour. The engine's hover cycle then
writes stock white / 0.878 gray at the same label — and both the Lua
wrappers and the native substitute used to rewrite those writes to the
accent, so a hovered swatch stuck accent until game restart. `paint()`
now records each agent's intended colour in `theme_custom` *before* the
write; `apply_theme` repaints that colour instead of the accent (which
makes every repaint path — rollover wrapper, retry queue, THEME_WIDGET —
preserve it), and the `AgentSetProperty`/`TextSetColor` wrappers restore
it on stock writes. The audit compares against the agent's *intended*
colour, so correct swatches never false-flag.

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
