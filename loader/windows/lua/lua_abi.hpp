// Shared game-Lua ABI declarations (clean-room, Lua 5.2 x86 cdecl).
// One copy used by lua_bridge.cpp and menu_bridge.cpp — same addresses,
// same calling convention, no second Lua runtime.
#pragma once
#ifdef _WIN32
struct lua_State {};
using lua_Alloc = void*(__cdecl*)(void*, void*, size_t, size_t);
using lua_CFunction = int(__cdecl*)(lua_State*);
using LuaNewstateFn = lua_State*(__cdecl*)(lua_Alloc, void*);
using LuaLoadstringFn = int(__cdecl*)(lua_State*, const char*);
using LuaPcallkFn = int(__cdecl*)(lua_State*, int, int, int, int, lua_CFunction);
using LuaGettopFn = int(__cdecl*)(lua_State*);
using LuaTolstringFn = const char*(__cdecl*)(lua_State*, int, size_t*);
using LuaPushCClosureFn = void(__cdecl*)(lua_State*, lua_CFunction, int);
using LuaSetglobalFn = void(__cdecl*)(lua_State*, const char*);
#endif
