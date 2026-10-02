# docs/third-party.md

Licensing matrix. Status as of 2026-09-16. No external code copied yet.

| Project | URL | License | Use |
|---|---|---|---|
| telltale_hook (HW12Dev) | https://github.com/HW12Dev/telltale_hook | No LICENSE file in repo (2 commits, WIP). **Treat as all-rights-reserved / Unknown.** Reference only. | Runtime reference: dinput8.dll proxy + `telltale_hook.dll` Lua-exec approach. MCSM1 compat flag `MINECRAFTSTORYMODE` confirmed. Do NOT copy until license clarified. |
| TelltaleToolKit (iMrShadow) | https://github.com/iMrShadow/TelltaleToolKit | MIT | Candidate for reuse: ttarch/ttarch2 read/write, Meta streams, resource contexts, D3DTX/D3DMesh/Lua/PROP, game DB. .NET (NuGet) - use as external tool/lib or port algorithms with attribution, not as C++ core. |
| TelltaleInspector (LucasSaragosa) | https://github.com/LucasSaragosa/TelltaleInspector | MIT (LICENSE.txt). Note: DEPRECATED, points to Telltale Editor. | Reference: PROP/SCENE/D3DMESH editors, TTARCH2 create/extract (oodle/zlib/none, encryption flag), BANK→OGG, Lua extract/compile. `Inspector/src/ToolLibrary` is the reusable core. |
| Telltale-Texture-Tool (Telltale-Modding-Group) | https://github.com/Telltale-Modding-Group/Telltale-Texture-Tool | MIT | External dependency for D3DTX↔PNG/DDS/TGA + JSON sidecars. Does NOT decrypt (pair with Telltale Explorer). Start at `D3DTX_V9.cs`. |
| D3DMESH-Converter / D3DMesh Editor | topic `telltale` on GitHub (Telltale-Modding-Group) | Varies per repo - verify per-repo LICENSE before reuse. | Reference only until validated against real MCSM1 meshes. RTB 3DSMax importer is the version-comparison baseline. |
| TTG-Tools (HeitorSpectre) | https://github.com/HeitorSpectre/TTG-Tools | **GPL-3.0**. Uploaded with permission of original authors (Den Em, Pashok6798). | Research/reference + optional external tool ONLY. Supports MCSM1 and MCSM2 (texts/landb/langdb/dlog, d3dtx, fonts, ttarch/ttarch2/obb, lua/lenc decrypt). Do NOT copy into MIT core. |
| Telltale Script Editor / Tweaks | Community (Telltale-Modding-Group) | Verify per repo | Reference for Lua build/priority/resdesc + per-game script API differences. |
| StoryForge (B0zin0) | https://github.com/B0zin0/StoryForge | No explicit license found (assume all-rights-reserved). | UX reference only: launcher/mod-manager, per-season exe paths, GameBanana mods. Not runtime. |
| MinHook (Tsuda Kageyu) | https://github.com/TsudaKageyu/minhook | BSD-2-Clause (LICENSE.txt kept) | **Vendored** under `third_party/minhook` (full src + include). Used for M2 detours. Attribution retained; binary redistribution reproduces copyright via docs. |
| miniz (Rich Geldreich et al.) | https://github.com/richgel999/miniz | MIT (LICENSE kept) | **Vendored** under `third_party/miniz` (src + include + static `miniz_export.h`). ZIP read/write for `.ttmod` (core package + CLI + runtime cache). |

Rules: prefer MIT reuse with attribution → clean-room reimplementation → external process → GPL integration only if whole-work license permits. Uncertain = document, don't copy.
