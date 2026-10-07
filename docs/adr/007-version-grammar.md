# ADR-007: Version grammar is bounded, not strict SemVer

**Status:** closed (Step 3, 2026-10-06)

## Context

Mod versions were compared by free-form dotted parsing:

```cpp
long num_prefix(const std::string& s);   // "2.0-beta" -> 2, stops at first non-digit
v = v * 10 + (c - '0');                  // unbounded: signed overflow is UB
```

Three defects came out of that. A version part longer than 18 digits
overflowed `long` (undefined behavior, so comparison could invert). A
non-numeric part silently compared as `0`, so `"1.x"` satisfied `>= 1.0`.
And nothing bounded input, so a manifest could carry a megabyte-long version.

The obvious fix is SemVer. It is the wrong fix here.

## Decision

Keep a **simplified compatibility format**, made total and bounded.

```
version    := core [ "-" prerelease ]
core       := part { "." part }        1..4 parts, missing = 0
part       := DIGIT { DIGIT }          1..9 digits, no sign, no spaces
prerelease := 1..32 chars of [0-9A-Za-z.-]
whole string <= 64 chars
```

Rules:

- Every core part must be fully numeric. `"1.0.0"` yes, `"1.x"` no.
- No numeric overflow: parts are capped at 999999999 and rejected above.
- **Prerelease is ignored for gating**: `"1.0.0-beta" == "1.0.0"`. Mods
  ship `"2.0-beta"` and gating must not break on it. The suffix must still be
  well-formed so garbage cannot pose as a version.
- **Build metadata is not part of the grammar.** SemVer's `+build` is
  rejected: it never affects gating here, and accepting it would need a
  second comparison rule for zero benefit.
- The empty string is a valid version equal to `0.0.0`. Manifests may omit
  `"version"` entirely, and an omitted version must compare rather than fail.
  `"-1.0"` and `"--"` are malformed (empty core with a dash).
- Comparison is total and deterministic: absent parts compare as 0, so
  `"1.2" == "1.2.0"`.

`Version` and `VersionConstraint` are now constructed only through
`parse()`, returning `Result`. `parse_manifest` validates both the
`version` field and every dependency constraint, so malformed input fails
the manifest instead of sorting as `0.0.0` later in the graph.

## Why not SemVer

Strict SemVer requires exactly three parts and treats prerelease as
ordering (`1.0.0-alpha < 1.0.0`). Both break existing mods: `"1.0"` and
`"1.2.3-beta"` are in the wild, and gating on prerelease would reject
builds that currently load. The value SemVer adds is build tooling we do
not have; the cost is breaking shipped mods.

`compare_versions(const std::string&, const std::string&)` stays for
display callers and sorts a malformed side as `0.0.0`. Validated paths
must use `Version::parse`.

## Consequences

- A mod declaring `"version": "1.x"` is now rejected at load, not silently
  treated as `>= 1.0` satisfied.
- Mods cannot declare more than four parts or a part above 999999999.
- Grammar constants live in `core/include/ttmod/version.hpp` next to the
  type, so parser and policy cannot drift.

## Amendment (PR 4)

This supersedes the earlier note: `compare_versions` has been removed in
favor of `Version::compare`. `ModIdentity` and `DependencySpec` now hold
`Version` and `VersionConstraint` values, and operator-only constraints are
rejected.