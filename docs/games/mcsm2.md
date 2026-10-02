# docs/games/mcsm2.md — mostly Unknown (honest)

- What we have: `Minecraft - Story Mode - Season Two [FitGirl Repack].zip` (3.4 GB, 14 entries, FitGirl `fg-*.bin` + `setup.exe`). NOT installed — no exe to inspect. Do NOT claim arch/build.
- External evidence: Season 2 PC requires **Windows 7 SP1 64-bit**, 3 GB RAM, 15 GB disk (Steam page via wiki). Released 2017-07-11, Telltale Tool, Lua. Strongly suggests **x64** PC build, but status = hypothesis until exe is installed and `parse_pe` is run.
- TTG-Tools and Texture Tool both list MCSM2 support — resource pipeline likely shared, versions may differ.
- telltale_hook has NO MCSM2 flag — hooking approach for MCSM2 is unproven.

```
MCSM2 profile: defined, not implemented, arch TBD (likely x64), feature support TBD.
```

## M13 research attempt (2026-09-17, blocked)
- FitGirl `fg-*.bin` are installer-internal (not listable/extractable with
  7z); the exe is only obtainable by running the interactive installer.
  Unattended install under Wine was not attempted (interactive GUI, 3.4 GB,
  uncertain outcome) — deferred until needed, NOT faked.
- Architecture already routes correctly by construction: any x64 PE detects
  as `unknown-x64` / `defined-not-implemented` (unit-tested), so an MCSM2 exe
  will be safely identified-but-idle on first contact.
- Format continuity (TTG-Tools/Texture-Tool list MCSM2) keeps the shared
  hypotheses alive: ttarch2 concept, D3DTX concept, Lua scripting.
- Blocker: MCSM2 executable unavailable in analyzable form. Unblocks M13/M14
  the moment an exe (any legitimate source) is present: run `ttmod detect`,
  record, compare, then profile + probe per the M9 playbook.

Next: install to isolated dir → run `ttmod-detect` → record size/timestamp/hashes → compare archive naming + ttarch2 header + Lua header vs MCSM1.
Shared-with-MCSM1 (hypothesis): ttarch2 concept, D3DTX concept, Lua-based scripting.
MCSM1-specific (known): x86 PE32 VS2010 build above.
Unknown: everything else about MCSM2's exe.
