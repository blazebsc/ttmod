# Security policy (TTMod)

## Trust model

- **Fully trusted:** the game install you point TTMod at, your own config
  files, mods you wrote.
- **Untrusted:** every `.ttmod` package and unpacked mod dir from anyone
  else — manifests, paths, configs, ZIP structure, sizes. All of it is
  validated before use (see below); validation failures disable the mod,
  never the game.
- **Explicitly dangerous by design:** native plugins (`.dll`). A validated
  plugin warning is printed at load and the game continues. Only use
  plugins you trust — they run arbitrary code.

## What the code guarantees

- One canonical validator each for mod IDs and mod-relative paths
  (`core/validate.*`): no absolute/drive/UNC paths, no `..` escape,
  no colons, depth and length caps. Packages and unpacked mods follow
  the same rules. Fuzzed (`tests/fuzz/`, CI smoke job).
- Strict JSON (vendored nlohmann/json): malformed input, trailing
  garbage, duplicate keys, and oversize documents are rejected.
  Unknown manifest fields are skipped (additive-only policy).
- Package caps: 512 MB file, 4096 entries, 64 MB/entry, 256 MB total,
  1 MB manifest. Symlinks/special files rejected.
- Atomic persistent writes (config) and transactional cache extraction:
  a crash never leaves parseable-looking partial state.
- Hook hot paths do no I/O, parsing, or unbounded allocation.
- The framework idles (vanilla game) on any unknown/unsupported build.

## Reporting

Security issues: open a GitHub issue titled `[security]` with a minimal
repro (package bytes or manifest text, never game files). Do not attach
proprietary game content.
