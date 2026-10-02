# Mod discovery (M11)

Single canonical source: `<game>/mods/`, scanned once per launch by portable
`discover_mods()` (unit-tested), then packaged entries resolve through the
cache. Legacy `plugins/` + `mods/`-as-scanned-dirs loader paths are retired;
everything flows through discovery + `ttmod/cache/`.

| Found in `mods/` | Behavior |
|---|---|
| `Foo.ttmod` (valid) | validated → cached → loaded like an unpacked mod |
| `Foo/` + `manifest.json` | used in place (dev/test, no repackaging) |
| `Foo.ttmod` + `Foo/` same ID | directory wins, package skipped (logged) |
| `README`/screenshots/`.dll`/`.zip`/dotfiles | ignored silently |
| broken `.ttmod` / bad manifest | skipped with `id: reason` |
| disabled (state or manifest) | skipped, counted, no runtime effect |

Order: id-sorted, deterministic. State (`config/mods.json`) applies before
depends/conflicts (M10 unchanged downstream).
