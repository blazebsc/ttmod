# MCSM1 build support (tested builds only)

| Build | Size | SHA256 (prefix) | FNV-1a64 | Profile | Status |
|---|---|---|---|---|---|
| Steam-era main (`MinecraftStoryMode.exe`, 2016-05-20) | 12179904 | `88443673…` | A11CD391555291BE | `mcsm1_pc_x86` | supported: bootstrap, plugins, res-mods, events |
| 3DM NoDVD copy | 12179904 | identical | identical | same as main | same as main (same bytes; crack-adjacent files not needed) |
| ALI213 NoDVD | 11724800 | `9143252f…` | E507F1E444839F62 | `mcsm1_pc_x86_ali213` | unrecognized: boots to idle, no hooks (by design) |
| CODEX NoDVD | 12179904 | `6a971853…` | 26B0BE1D74787BCD | `mcsm1_pc_x86_codex` | unrecognized: idle (runtime untested) |

Only the main build is moddable. Limits on main: ~4 s menu lifetime in
the test env; packed exe (RE notes apply).

## Build identity rule
Size+timestamp ALONE misidentifies the CODEX crack as the supported
build (same size/sections/timestamp, different bytes). Identity = size
+ timestamp + FNV-1a64 (+ SHA256 pinned externally). Caught by
`ttmod-detect` before it could matter at runtime.

## Unknown builds
Unknown-shape builds get `mcsm1_pc_x86_variant` / `unknown(-x64)`:
the framework idles with a clear log, game untouched. Never hooked,
never guessed. New builds require zero framework changes: only profile
data + per-build validation (signatures stay build-specific by design).

## Proven (Wine)
ALI213 boots with framework: `Profile: mcsm1_pc_x86_ali213
status=unrecognized-build` → `framework idle` → no hooks → exit 0.
(Needed its dir-local `fmod.dll`/`fmodstudio.dll`; crack dirs ship
without.) CODEX runtime idle-test: NOT run (same idle path as ALI213
by construction; detection proven offline). Marked Unverified, not
claimed. Hooking non-A builds is future work (needs per-build unpack
dumps; no MCSM1 address is assumed to transfer).

## Explicitly not supported
- MCSM Season 2: executable unavailable; would detect as unknown /
  `defined-not-implemented` and idle (arch TBD — see `mcsm2.md`, a
  frozen research log).
- Other Telltale games: no profiles, no testing, no claims.
- Archive-internal overrides: engine reads past CreateFileW (limitation).
- Anything requiring Lua VM internals: research only.
