---
kind: game
title: Hooking a Telltale engine (Minecraft Story Mode) with a proxy DLL and a Lua bridge
game: 'Minecraft: Story Mode'
games_also: []
game_version: 'MCSM1 x86 (mcsm1_pc_x86, build 101 2024-RC 2016.05.23, SHA256 88443673…27817f7), Season 1, Steam/pirated retail install, tested under Wine'
platform: windows
engine: unknown
route: native-hook
tools: ['cmake', 'mingw-w64-gcc (Arch)', 'MinHook', 'miniz', 'TelltaleToolKit (archive reading only)', 'python3 + lua5.1/5.2 for offline proofs']
anti_cheat: 'none - single-player, offline. No DRM/anti-cheat was present or touched.'
status: working
agents:
- OpenCode (space-bunny-free)
humans: []
date: '2026-10-03'
links: []
tags: [proxy-dll, packed-exe, lua, minhook, mingw-cross, telltale, menu-ui, agent-properties]
---

# Telltale engine (Minecraft Story Mode) - reverse-engineering field notes

> This doubles as a field note in the [universal-modder](https://github.com/rehan-remade/universal-modder)
> knowledge base (`um kb new --game "Minecraft: Story Mode"`), where there was no prior Telltale entry.
> Kept here because it is the project's memory: the engine facts below cost a full debug cycle each and are
> invisible from static inspection.

> A working mod-loader framework for Minecraft: Story Mode, built clean-room with no game files shipped.
> Route: `dinput8.dll` proxy + MinHook detours, verified in the real game by the human in an interactive
> session. Ships an in-game Mods menu, resource overrides, a plugin ABI, and config-driven menu theming.

## Setup

- **Game:** MCSM1, x86 PE32. The build is identified by PE machine + timestamp + size + FNV-1a64, not by
  name. Unknown/cracked variants (NoDVD/ALI213/CODEX) boot to **idle by design** — the framework detects but
  does not hook, so the game still runs vanilla.
- **Toolchain:** the DLLs are cross-compiled i686 MinGW-w64 from Linux. The system toolchain
  (`mingw-w64-gcc` on Arch/CachyOS, `mingw-w64` on Debian/Ubuntu) provides `i686-w64-mingw32-{gcc,g++}`.
  CI installs it via apt, so local and CI build the same way.
- **Validation gate:** a script checks both DLLs are PE32/i386, export an undecorated `DirectInput8Create`,
  and import **only system DLLs** (no game DLLs). Run it after every win32 build; it catches accidental
  static linking of something odd.
- **Runtime proof needs a human.** Headless/Xvfb Wine exits before the menu ~99% of the time. All visual
  verification was done by the human launching the game and reading `logs/ttmod.log`.

## Route and why

`native-hook`. Rejected: a resource-only override route (too narrow - it cannot change behaviour, and the
whole point was a menu), and reimplementing the engine (weeks). The proxy DLL is the cheapest thing the
Windows loader will run for us, and the game imports DirectInput, so `dinput8.dll` is picked up for free
with no exe patching.

The engine is a proprietary Telltale engine (Lua 5.x scripting, `Agent*` property system, `Menu_Add`-based
menus). No public modding framework for it. The community reference project `telltale_hook` is unlicensed, so
nothing was copied from it - only its broad approach was noted, and clean-room reimplementation used.

## How the game works (what we had to learn)

**Injection.** The exe is packed and unpacks progressively at runtime. Consequences:
- Never detour game code at process init. Doing so hangs Wine reproducibly. File-IO hooks early; everything
  else installs late (~2.5 s), after unpacking.
- Addresses in the packed file bytes are wrong. Find functions by AOB byte pattern and **validate the anchor
  against live memory** before patching, retrying a few times. If the pattern does not match, install nothing
  and log it.

**Two Lua states.** The game runs engine scripts (`_engine.lua`, `StoryBoardTracker.lua`) and menu scripts
(`Menu.lua` and the states menus are built in) in **separate `lua_State`s**. Anything a plugin sets as a
global during a script-load callback does not exist where menus are built. This is the single most surprising
fact in the note.

**The Lua runtime is 5.1 despite version strings.** Exe strings say 5.2.3, but at runtime
`setmetatable` and `_G` are both **nil**. It behaves as a stripped 5.1 environment. `_G` being nil is brutal:
`_G.Foo` throws, and inside an engine callback that kills the process instead of raising a catchable error.
Use bare globals.

**The widget property system.** UI is built with `Menu_Create(ListMenu, ...)`, `Menu_Add(Header|ListButton, id,
label, cb)`, `Menu_Pop()`, and populated **inside** `menu.Populate` (which runs on `Menu_Push`; rows added
before a push land on the *current* menu and vanish). Properties go through `AgentSetProperty` /
`AgentGetProperty` on a widget's `.agent`. `Clone_Find` **throws** on a wrong target and wants the agent, not
the widget table - always wrap engine calls in `pcall`.

- The label text property is exactly `Text Color` - value is a **table with named fields,
  0..1 FLOATS**: `{ r = ri/255, g = gi/255, b = bi/255, a = 1 }`. Integers 0..255 get CLAMPED and render
  stock, which masquerades as "the engine overwrote us". A positional array `{r,g,b}` and a hex string
  are silently ignored. Guessed names (`Color`, `Tint Color`, `Font Color`, `Diffuse`) do **not** exist
  on a label; only `Text Color` read back.
- **Hover/press highlight is engine-owned.** After the float fix the write provably holds
  (`verify-ok (exact)`), yet hover still reverts: a 297-name read-only sweep found no state property, and
  the exe contains no hover concept (`MouseClick` only). The row-highlight is drawn by the engine from
  internal state; changing it needs a native detour, not a Lua property.

**Script crypto does not work (unresolved).** Loose scripts are `\x1bLEo` + opaque bytes; archived ones
`\x1bLEn` + opaque. The widely repeated recipe "strip the 4-byte magic, Blowfish with the profile key
`Mcsm`, prepend `\x1bLua`" **does not decrypt them** - verified: different inputs decrypt to the identical
head, which real encryption cannot do. Do not trust older write-ups of this; the container reports
`IsRawDeflateCompressed` (not encrypted) while entry bytes remain random. Offline decryption is unsolved, so
**read the engine's own UI behaviour at runtime instead** - see below.

## Build steps

```sh
# native tests
cmake -S . -B build && cmake --build build && ctest --test-dir build

# win32 cross-build (separate dir, never mixed)
cmake -S . -B build-win32 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake \
  -DTTMOD_BUILD_WIN_RUNTIME=ON && cmake --build build-win32
sh tools/verify_win32.sh build-win32

# deploy (after the gate passes)
cp build-win32/dinput8.dll build-win32/ttmod_framework.dll "<game dir>/"
```

A mod is a folder in `mods/` with `manifest.json`, or a `.ttmod` zip. Config lives in `config/<mod-id>.json`.

## Verification

Oracle: the human launches the real game, clicks through the in-game Mods menu, and the framework writes
every decision to `logs/ttmod.log`. Verified working in the real game: mod discovery, enable/disable,
per-mod config, resource overrides, the in-game Mods menu (list -> details -> config rows), and
config-driven label tinting. **Not** verified: hover/press tint states (revert to stock - open),
MCSM2, and any offline script decryption.

## Gotchas

1. **A mod's plugin never loads, silently.** A `plugins/` folder with no `"plugin"` key in the manifest
   loads as resource-only; the mod appears in the menu and does nothing, with no warning. Declare
   `"plugin": "plugins/<name>.dll"`. The loader now logs this case. Always confirm
   `plugins: <id> initialized (rc=0)` appears in the log; zero `plugins:` lines means it is not loading.
2. **Two Lua states.** Plugin globals set on the engine state do not exist on the menu state. Replay queued
   chunks per menu state.
3. **`_G` is nil.** Any `_G.X` throws, and in a callback that kills the process. Bare globals only.
   `setmetatable` is nil too (5.1).
4. **The runtime is 5.1, but stock `lua5_1` and `lua5_2` both provide `_G` and `setmetatable`.** An offline
   harness running the real Lua file will stay green while the game is broken. Run proofs under **both**
   interpreters *and nil out `_G`* in the stubs.
5. **Declaration order is load-bearing in game Lua.** A reference to a later `local` is a nil global at call
   time, and inside a callback that is a hard process kill, not an error. This crashed the game twice.
6. **Do not `tostring()` an engine agent.** Agents are userdata; `__tostring` can fault. Log property
   names, never the object. Compare read-back values only when `type()` says string/number.
7. **Never spray unknown property names during a screen build.** Discover properties once, explicitly, then
   reuse the proven one. A per-label retry loop over guesses is what crashed a menu.
8. **The engine renders a fixed number of list rows per menu.** Rows past that are added but render blank
   (hovering one shows its label). Paginate any generated list, e.g. a colour picker.
9. **A labelled colour property does not imply a state-specific one exists.** A
   297-name read-only sweep found only `Text Color`; no hover/press variant
   exists, and the exe has no hover concept at all. The white you see on hover
   is the engine's own row-highlight rendering. Themed base colour verified
   holding exactly (0..1 floats); hover tint is an engine-owned limitation
   unless a native detour is authorized.
10. **`AgentGetProperties` returns `(type-name, props-table)`-shaped values;**
    accept answers from ANY return, not the first. Its first return is a type
    name (`__ScriptObject`), which a first-value-only parser mistakes for the
    answer.
11. **Menus have a hard row budget** and generated palettes must be paged or they render as empty boxes.

## Assets

None. No art, audio, or 3D were needed; the framework ships no game content.

## Why this note exists

There was no prior field note for any Telltale game. The engine-specific facts above (two Lua states, nil
`_G`, `Text Color` as a named-field table, the packed-exe detour timing, and the plugin-declaration trap)
each cost a full debug cycle to find and are all invisible from static inspection.
