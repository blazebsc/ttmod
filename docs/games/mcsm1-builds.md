# M9 MCSM1 builds (multi-build model PROVEN)

## Known PC builds (all x86, timestamp 1463779093, VS2010/Linker 10.0)

| Build | Size | Sections | FNV-1a64 | SHA256 (prefix) | Profile | Status |
|---|---|---|---|---|---|---|
| main (+3DM copy) | 12179904 | 6 | A11CD391555291BE | 88443673… | mcsm1_pc_x86 | supported (hooks) |
| ALI213 NoDVD | 11724800 | 5 | E507F1E444839F62 | 9143252f… | mcsm1_pc_x86_ali213 | unrecognized (idle) |
| CODEX NoDVD | 12179904 | 6 | 26B0BE1D74787BCD | 6a971853… | mcsm1_pc_x86_codex | unrecognized (idle) |

## Lessons
- Size+timestamp ALONE misidentifies the CODEX crack as the supported build
  (same size/sections/timestamp, different bytes). Identity = size + timestamp
  + FNV-1a64 (+ SHA256 pinned externally). Caught by `ttmod-detect` before it
  could matter at runtime.
- Unknown-shape builds get `mcsm1_pc_x86_variant` / `unknown(-x64)`: framework
  idles with a clear log, game untouched.

## Proven (Wine)
- ALI213 boots with framework: `Profile: mcsm1_pc_x86_ali213
  status=unrecognized-build` → `framework idle` → no hooks → exit 0.
  (Needed its dir-local `fmod.dll`/`fmodstudio.dll`; crack dirs ship without.)
- CODEX runtime idle-test: NOT run (same idle path as ALI213 by construction;
  detection proven offline). Marked Unverified, not claimed.
- New builds require zero framework changes: only profile data + per-build
  validation (signatures stay build-specific by design).

## Not done (future M9 work)
Signature/porting work to HOOK non-A builds (needs per-build unpack dumps).
No MCSM1 address is assumed to transfer.
