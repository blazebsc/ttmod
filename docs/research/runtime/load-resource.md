# docs/research/runtime/load-resource.md - ScriptManager::LoadResource (M2, VERIFIED)

## Status
**Resolved, validated, and fired once (m2e run, 32 resdesc paths); detour
code path since REMOVED.** MinHook detours on unpacked game code hang Wine
reproducibly (thread-freeze racing the unpacker - 2 hangs). Runtime file-IO
and content control now go through a packer-proof IAT hook on
`CreateFileW` (M2-completing hook, M4 rewrite, M5 path). The signature below
is preserved for a post-unpack M6 attempt. No game code is patched.

Update (M6+): the M6 attempt succeeded - late (2.5 s, anchor-verified)
MinHook detours on game `lua_newstate` + `ScriptManager::LoadResource` are
installed by `loader/windows/lua_bridge.cpp` and drive the shipped in-game
Mods menu (see `docs/runtime/in-game-mod-menu.md`). The IAT hook above
remains the file-IO path; the detours below are the Lua path.

## Known
- imgRVA `0x1139F0` in the mcsm1_pc_x86 build (SHA256 `88443673…27817f7`).
- cdecl `int (lua_State*, const char*)`: two stack params, hook reads a valid
  script path on every hit, original returns normally, game continues (exit=0,
  identical to baseline).
- Hook fires 32/32 capped hits at boot with `_resdesc_50_<Archive>.lua` paths -
  this function loads per-archive resource descriptors. `50` is presumably the
  resource/priority version for this engine build (M5/M6 lead).

## Observed
- Disk `.text` entropy 8.00 (packed); in-memory entropy 8.00 at process-attach,
  ~6.4 at +2s → the packer decrypts **progressively during startup**.
- An early-init read of the RVA showed packed bytes (`0F 70 CA…`, first byte
  `0x0F`); the same address post-unpack is a real MSVC SEH/cookie function
  (`55 8B EC 6A FF 68 <scope-table> 64 A1 00…`, /GS cookie, 2 stack params).
- First hypothesis ("RVA wrong for this build") was **disproven** by the
  unpack-aware re-test; corrected hypothesis: RVA right, timing wrong.
- Gotcha: in-memory `.text` dump is section-relative (section RVA `0x1000`);
  imgRVA `0x1139F0` = dump offset `0x1129F0`.

## Signature (unpack-aware, validated unique)
`55 8B EC 6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 81 EC 44 02 00 00 A1 ?? ?? ?? ?? 33 C5 89 45`
(32 B, 8 wildcards over scope-table + cookie addresses). Exactly 1 hit in the
unpacked 8.5 MB `.text`, at imgRVA `0x1139F0`. Poller: every 250 ms up to ~30 s,
require match + uniqueness, then MinHook install. Timeout → log + no hook.

## Calling convention / params
cdecl, `(lua_State* L, const char* filename)`. Matches telltale_hook's x86
declaration; behaviorally confirmed (readable paths, stable game).

## Validation gates (all must pass or no hook)
profile == mcsm1_pc_x86 → headers sane → .text located → signature matches at
RVA → signature unique in .text → MinHook create+enable ok.

## Runtime result (Wine 11.17)
`hooks: installed ScriptManager::LoadResource @ RVA 0x1139F0 (sig unique)`,
then `[LoadResource#N] …\archives\_resdesc_50_*.lua`. Game exit=0, same as
unhooked baseline. Companion CreateFileA detour (import-verified, no RVA) also
fires (`openssl.cnf` - game barely uses the A-variant; W-variant noted for M5).

## Wine observations
No Wine-specific hook issues. `CreateThread` from the watch path works;
MinHook trampoline executes; TLS reentrancy guard quiet (no recursion seen).

## Unknown / next
- Full function body semantics beyond "loads resdesc lua" (M6).
- Whether the RVA holds for other MCSM1 builds (M9 will re-validate per build).
- `lua_newstate` anchor from the same table still unverified (bytes `8A 38 F7 FC`
  at init - likely packed; needs unpack-aware check, not yet done).

## Confidence
High for this build. Nothing here is assumed to transfer to other builds.
