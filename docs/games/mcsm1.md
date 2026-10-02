# docs/games/mcsm1.md — observed, not assumed

Source: `Minecraft - Story Mode/MinecraftStoryMode.exe` + `archives/` in this workspace.

- Format: PE32, i386 (machine 0x014C, opt magic 0x10B), GUI, 6 sections.
- Size: 12179904 bytes. mtime 2016-06-23. COFF timestamp 1463779093 (2016-05-20T21:18:13Z).
- Linker 10.0 (VS2010), ImageBase 0x400000, DYNAMIC_BASE|NX_COMPAT|TS_AWARE. Entry 0x00FB3310 (objdump).
- Hashes: SHA256 `88443673...27817f7`, MD5 `171ff4fe90b4a5146d76907984839a47` (see shell history; re-verify with `sha256sum`).
- Engine/libs: `Telltale` string @0x82d704; FMOD (`fmod.dll`, `fmodstudio.dll`, `FMOD::System::getVersion` import); Lua 5.2.3 (`$LuaVersion: Lua 5.2.3 ...`, `LUA_PATH_5_2`); VERSION.dll imports.
- Scripts: `AdventurePass.lua` starts `1B 4C 45 6F ...` (`\x1bLEo`) — Telltale encrypted/compiled Lua, NOT stock `1B 4C 75 61`. `MCSM_pc_Engine_Lua_data.ttarch2` + `_boot.lua`/`_project.lua` strings in exe.
- Archives: 328 entries, `MCSM_pc_*_<stream>.ttarch2` (`Boot/Menu/Minecraft101..108/JesseMale...` × `anichore/data/ms/txmesh/voice/compressed/dlog/uncompressed`). `MCSM_pc_Boot_data.ttarch2` header `5A 43 54 54 00 00 01 00 03 ...` ("ZCTT"). Naming = `{Game}_{pc}_{Episode}_{stream}`.
- Resource strings in exe: `*.prop`, `module_*.prop`, `.scene/.chore` handled via archives; `ttcache:prefs.prop`.
- Cracks present in tree (`NoDVD/3DM,ALI213,CODEX`) — detection must NOT depend on crack DLLs; identify by exe bytes only.

Confidence: high for arch/build/Lua-version/archive-naming. Medium: exact engine revision (no `v28`-style string found; needs PDB/symbol work).
Next tests: ttarch2 header parse vs TelltaleToolKit; Lua decrypt via TTG-Tools externally; import-table dump for dinput8-proxy feasibility.
