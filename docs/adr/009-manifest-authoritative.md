# ADR-009: Validated manifests are authoritative

**Status:** closed (Roadmap PR 5)

## Context

Manifest consumers repeated checks for values that had already crossed
validation, including host compatibility and the safety of paths. This
allowed consumers to disagree about whether a manifest was usable and left
raw path spellings in trusted data.

## Decision

`RawManifest` represents syntax only. `validate_manifest` is the only
producer of a trusted `ModManifest` and establishes its invariants:

- The mod id is valid; versions and dependency constraints are typed.
- Plugin, replacement, and entrypoint paths are normalized mod-relative
  paths. Game-side replacement keys remain as written for the resolver.
- Games are well-formed and unique.
- Dependencies and conflicts are non-self, unique, and non-contradictory.
- Runtime and permission values are known and unique.
- Entrypoints agree with runtimes when the entrypoints field is present.
- The config schema is validated.

Consumers rely on these invariants and do not re-validate manifest fields.
Host-specific compatibility is answered by `ModCompatibility`, with the
host API, game and season, or architecture passed in by the caller.

## Consequences

Manifest policy has one validation boundary. Consumers can make host
compatibility decisions through the manifest type without duplicating its
rules or embedding host values in validation.
