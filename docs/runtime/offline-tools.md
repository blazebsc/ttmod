# M12 offline tooling (no runtime changes)

## `ttmod inspect <file.ttarch2>`
TTARCH2 header probe: magic, size, raw `u16[4]` / `u64[3]` / 16-byte tag.
Field semantics are Unknown (payload format = future work; references:
TelltaleToolKit MIT, TTG-Tools GPL external-only). Rejects non-archives.
Example (MCSM_pc_Boot_data.ttarch2): `ZCTT`, `{0,1,3,0}`, `{44,1171,12530}`.

## `ttmod map <ttmod.log>`
Boot file-open map: totals by category/ext, first-seen resdesc/archive/save
lists. Replaces hand analysis; proven identical output on a real 400-open log.

## `ttmod package ...`, `ttmod detect`, `ttmod mods ...`
Dev/advanced CLI (normal users never need it): create/validate/info packages,
detect exes, list/info/enable/disable mods (state file edits).

## Policy
Offline tools never modify game files; extraction targets are explicit;
no RE claims beyond demonstrated parsing (see test_offline).
