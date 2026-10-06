# M5 resource overrides (PROVEN under Wine)

## Architecture
```text
Game CreateFileW → IAT hook → normalize → Resolver (index) → hit?
  yes → real CreateFileW(replacement, ...same args...) + log override
  no  → real CreateFileW(original, ...same args...) (misses silent by default)
```
Resolver (`core/resolver.*`, portable, unit-tested) is separate from the
Windows hook (`loader/windows/hooks/hooks.cpp` + `mods.cpp`). Hot path = one map
lookup; index built once at init; no IO per request. Only the filename may
change - access/share/security/disposition/flags/template pass through.

## Path normalization (`core/pathnorm.*`, unit-tested)
`\\?\`/`\\?\UNC\`/`\??\` stripped → `\`→`/` → collapse seps (UNC `//` kept) →
drop `.` → resolve `..` lexically with root floor (drive `x:` unpoppable,
UNC server+share unpoppable, above-root dropped, relative `..` preserved) →
ASCII lowercase → strip trailing `/` (bare roots kept).
Internal keys use `/`; converted back to `\` only for the real API call.

## Scope policy
Only requests under the game root (exe directory) are eligible. Anything else
(`outside-root`) passes through untouched. DLLs under the root are technically
eligible - overriding them equals native-code trust (documented, not blocked).

## Mod layout (unified manifest)
```text
mods/<id>/manifest.json   resource-only (never loads DLLs)
plugins/<id>/manifest.json  native (+ optional "files" → hybrid)
```
```json
{"id":"...","api":1,"games":["minecraft-story-mode:s1"],
 "priority":200,"enabled":true,
 "files":{"archives/x.lua":"files/archives/x.lua"}}
```
Game paths: root-relative or absolute. Replacements: relative subpaths only;
`join_checked` rejects escapes/absolute/drive forms at index time; missing
files rejected at index time (existence checked once, not per open).

## Priority & conflicts
Higher number wins; ties → smallest mod id (deterministic). Every conflict is
logged once at resolve time (`RESOURCE CONFLICT … winner … losers remain
visible`). Disabled mods (`enabled:false`) never enter the index.

## Safe failure
Invalid override → problem logged (`mods: problem: …: <reason>`) → original
file used. Game never sees an error for a bad mod.

## Diagnostics
Default: overrides, conflicts, problems, index summary. `TTMOD_RESOLVE_VERBOSE=1`
logs every resolution with reason/winner/shadowed count.

## Supported / unsupported (observed)
- Loose resdesc replacement: WORKS (German108→German107 consumed, boot normal).
- Anything served through an already-open archive handle bypasses CreateFileW
  and is NOT overridable at this layer (limitation; next layer is M6 engine
  research, not archive rewriting).
- Symlink/reparse escape: lexical check only (documented limitation).

## Proof (Wine matrix `tools/wine_matrix.sh`, all exit 0)
baseline · framework-only · resolver-empty(0 paths) · valid mod (override
fired, mod file opened, original untouched md5-OK) · conflict (p200 beats
p100, logged) · invalid+disabled (3 problems logged, skipped) · resdesc.
Durations 4–8s in all cases, no systematic slowdown vs baseline.
