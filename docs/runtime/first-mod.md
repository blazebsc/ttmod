# M4 first real modification (PROVEN under Wine)

Two independent proofs that the running game is modified, not merely observed.

## 1. Resource rewrite at the file-IO layer (primary)
With `TTMOD_M4_REWRITE=1`, the CreateFileW hook redirects one boot load:
```text
[M4] rewrite: \\?\h:\...\archives\_resdesc_50_German108.lua
           -> \\?\h:\...\archives\_resdesc_50_German107.lua
```
The engine opens German107 twice and German108 never. Game exits 0.
Default (env unset): pure passthrough. Permanent override system is M5;
this demo stays env-gated in `hook_CreateFileW`.

## 2. Visible window title (example plugin `examples/title-mcsm`)
`title.mcsm` finds the `Telltale Games` window and prefixes it:
```text
title-mcsm: title changed: 'Telltale Games' -> '[TTMod] Telltale Games'
  (readback '[TTMod] Telltale Games')
```
Cosmetic only. Polls `FindWindowA` (a `WH_CBT` variant was tried and
REVERTED: threadId=0 hooks are desktop-wide in Wine and correlated with
hangs/early exits — see below).

## Incidental discoveries (M5 leads)
- Engine opens resdesc loose files via `CreateFileW` with `\\?\` prefix:
  `_resdesc_50_<Archive>.lua` per archive stream (Boot/Menu/Minecraft101..108/
  JesseMale*/Chores*), plus `_rescdesc_50_version_101/102.lua` (note the
  engine's own `rescdesc` typo).
- Boot opens ~45+ resdesc files in deterministic order; game barely uses
  the A-variant (`openssl.cnf` once — Steam/WININET, not the engine).

## Wine anomalies (Unknown, recorded for M7)
- In-process `EnumWindows` never enumerates the game window although X11
  shows it mapped (`Telltale Games`, 2559x1600). `FindWindowA` exact-match
  works. Event system must not rely on enumeration.
- `WH_CBT` threadId=0 hook installed but never delivered callbacks; runs
  with it correlated with instability. Avoid global hooks under Wine.
- Game exits 0 after ~4s in this environment with and without the framework
  (identical wine-log depth) — menu lifetime is an environment property,
  not a framework effect.
