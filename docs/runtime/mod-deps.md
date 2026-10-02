# M10 dependencies / load order (PROVEN under Wine)

## Discovery
One shared `scan_mods` (plugins/ + mods/) feeds both loaders: same registry,
id-sorted (deterministic — proven by alphabetical discovery logs), single
manifest validation point (api 1..HOST, game match, enabled).

## Declarations (manifest, all optional)
```json
"depends": [{"id": "base.mod", "version": "2.0"}, {"id": "opt"}],
"conflicts": ["rival.mod"]
```
Version = minimum, dotted-numeric compare (`compare_versions`, unit-tested;
non-numeric tails ignored, missing parts are 0).

## Enforcement (`check_requirements`, portable, unit-tested)
- Missing dep / version-too-low / conflicting-present-mod → rejected with a
  human reason, logged, mod skipped. No auto-resolution (documented).
- Applies to native AND resource mods uniformly (both loaders gate on it).
- Asymmetric by design: A conflicting B blocks A; B is unaffected unless it
  also declares.
- present-set = enabled + api/game-valid manifests across both dirs.

## Proven (Wine)
```text
conflictme rejected (conflicts with present mod: german108.override)
dep-missing rejected (missing dependency: no.such.mod)
dep-ver rejected (dependency german108.override version 1.0.0 < 9.9)
```
dep-ok (satisfied dep) loads silently; override still fires; exit 0.

## Not done (future)
Transitive resolution/install, version ranges beyond minimum, load-order
beyond priority+id sort, capability requirements. Priorities (M5) unchanged.
