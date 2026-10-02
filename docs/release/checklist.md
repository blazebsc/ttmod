# Release checklist (TTMod)

## Build
- [ ] `cmake -S . -B build && cmake --build build && ctest` - all pass
- [ ] Win32 cross-build passes (`TTMOD_BUILD_WIN_RUNTIME=ON`)
- [ ] `tools/verify_win32.sh build-win32` - PE32/i386, exports, system-only imports
- [ ] `tools/make_release.sh` produces `TTMod-<ver>/` (DLLs + examples + README + LICENSE)

## Runtime (Wine, real MCSM1)
- [ ] fresh install: dirs created, game starts, log clean
- [ ] packaged mod: discovered → cached → override consumed
- [ ] unpacked mod: identical behavior
- [ ] plugin mod: warning + init, game continues
- [ ] hybrid mod: native + files together
- [ ] dependencies/conflicts enforced with reasons
- [ ] invalid/disabled mods skipped safely
- [ ] safe mode (env + file): nothing third-party loads
- [ ] hostile packages rejected (traversal/abs/dup/no-manifest/arch)
- [ ] full `tools/wine_matrix.sh`: all exit 0, originals md5-OK, dir clean

## Docs & legal
- [ ] README user-first; user/{install,mods,troubleshooting}.md current
- [ ] `docs/third-party.md` lists miniz (MIT) + MinHook (BSD) + imgui (MIT) + references
- [ ] release contains no game files, no proprietary assets, no dumps
- [ ] LICENSE + THIRD-PARTY.txt in release dir

## State
- [ ] game dir pristine after matrix (only original files)
- [ ] no fake support claims (MCSM2/other games explicitly out)
