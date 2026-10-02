# MCSM1 compatibility (tested builds only)

| Build | Arch | SHA256 (prefix) | Profile | Bootstrap | Plugins | Res-mods | Events | Limits |
|---|---|---|---|---|---|---|---|---|
| Steam-era main (`MinecraftStoryMode.exe`, 2016-05-20, 12179904 B) | x86 | `88443673…` | `mcsm1_pc_x86` supported | ✅ | ✅ | ✅ | ✅ | 4 s menu lifetime in test env; packed exe (RE notes apply) |
| 3DM NoDVD copy | x86 | identical | same as main | ✅ (same bytes) | ✅ | ✅ | ✅ | crack-adjacent files present; not needed |
| ALI213 NoDVD | x86 | `9143252f…` | `mcsm1_pc_x86_ali213` unrecognized | idle only | - | - | - | boots to idle, no hooks (by design) |
| CODEX NoDVD | x86 | `6a971853…` | `mcsm1_pc_x86_codex` unrecognized | idle only (runtime untested) | - | - | - | size+timestamp identical to main: needs FNV identity |

Only the main build is moddable. Unknown variants idle safely by design -
never hooked, never guessed.

## Not supported (explicit)
- MCSM Season 2: executable unavailable; x64 would detect as `unknown-x64`
  / `defined-not-implemented` and idle.
- Other Telltale games: no profiles, no testing, no claims.
- Archive-internal overrides: engine reads past CreateFileW (limitation).
- Anything requiring Lua VM internals: research only.
