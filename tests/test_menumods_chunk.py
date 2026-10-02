#!/usr/bin/env python3
"""Dynamic Menu_Mods chunk test on stock Lua 5.2 (via nix-shell, same
interpreter family as the game's 5.2.3 for this 5.1-compatible chunk).
Proves exists/called/returned marker semantics the bridge relies on."""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Chunk text is extracted from loader/windows/menu_bridge.hpp (single
# source of truth, loader-owned), then executed for real.
HDR = os.path.join(ROOT, "loader", "windows", "menu_bridge.hpp")
with open(HDR, encoding="utf-8") as f:
    hsrc = f.read()
i = hsrc.index("kMenuModsChunk")
j = hsrc.index(";", i)
import re

CHUNK = "".join(re.findall(r'"([^"]*)"', hsrc[i:j]))
assert "function Menu_Mods()" in CHUNK, CHUNK
# Static pins (moved from tests/test_lua_bridge.cpp with the chunk).
for needle in ("ttmod_mods_calls", "ttmod_mods_pressed", "if Menu_Options then"):
    assert needle in CHUNK, needle
DRIVER = (
    CHUNK + "\n"
    "assert(type(Menu_Mods) == 'function', 'exists')\n"
    "Menu_Options = function() ttmod_options_hit = true end\n"
    "Menu_Mods()\n"
    "Menu_Mods()\n"
    "assert(ttmod_mods_calls == 2, 'called twice, got ' .. tostring(ttmod_mods_calls))\n"
    "assert(ttmod_mods_pressed == true, 'pressed marker')\n"
    "assert(ttmod_options_hit == true, 'transition ran')\n"
    "print('menumods-chunk: exists+called+returned OK')\n"
)

with open(os.path.join(ROOT, "loader", "windows", "menu_bridge.hpp"), encoding="utf-8") as f:
    src = f.read()
for needle in ("kMenuModsChunk", "kMenuModsCalls", "kMenuModsPressed", "bridge_run_chunk"):
    assert needle in src, needle

r = subprocess.run(
    ["nix-shell", "-p", "lua5_2", "--run", "lua -e " + "'" + DRIVER.replace("'", "'\\''") + "'"],
    cwd=ROOT,
    capture_output=True,
    text=True,
    timeout=300,
)
sys.stdout.write(r.stdout)
sys.stderr.write(r.stderr)
if r.returncode != 0 or "exists+called+returned OK" not in r.stdout:
    print("FAIL: Menu_Mods chunk did not prove on stock Lua 5.2")
    sys.exit(1)
print("menumods-chunk: dynamic proof passed")
