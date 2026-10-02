#!/bin/sh
# M5/M10/M11 Wine matrix. Usage: sh tools/wine_matrix.sh "<game dir>"
# Stages framework DLLs + test mods beside the game, runs cases, cleans up.
# Game files are never modified (md5-checked before/after).
set -u
G="${1:?usage: wine_matrix.sh <game dir>}"
T="$(dirname "$0")/.."
A107="$G/archives/_resdesc_50_German107.lua"
A108="$G/archives/_resdesc_50_German108.lua"
BOOT="$G/archives/_resdesc_50_Boot.lua"
LOG="$G/logs/ttmod.log"
echo "--- originals ---"
md5sum "$A107" "$A108" | tee /tmp/opencode/m5_orig.md5
FW="$T/build-win32"
stage_fw() { cp "$FW/dinput8.dll" "$FW/ttmod_framework.dll" "$G/"; }
unstage_all() { rm -rf "$G/dinput8.dll" "$G/ttmod_framework.dll" "$G/mods" "$G/config" "$G/logs" "$G/ttmod"; }
run_case() { # name
  rm -f "$G/logs/ttmod.log" "$G/logs/ttmod.exit.log"
  S=$(date +%s)
  ( cd "$G" && timeout 60 wine MinecraftStoryMode.exe > "/tmp/opencode/m5_$1.txt" 2>&1 )
  ec=$?; dur=$(( $(date +%s) - S ))
  echo "== $1: exit=$ec dur=${dur}s =="
}
chk() { grep -E "$2" "$LOG" 2>/dev/null | head -n "$3" | sed "s/^/    /"; }

unstage_all
echo "### 1 baseline (no framework)"
run_case baseline
echo "### 2 framework only"
stage_fw; run_case fw-only
chk fw-only "profile:|installed Create" 3
echo "### 3 resolver, no mods"
run_case resolver-empty
chk resolver-empty "index ready" 1
echo "### 4 valid mod (german108-override, unpacked)"
M="$G/mods/german108-override"; mkdir -p "$M/files"
cp "$T/tests/fixtures/german108-override/manifest.json" "$M/"
cp "$A107" "$M/files/_resdesc_50_German108.lua"
run_case valid-mod
chk valid-mod "indexed|override: german" 4
echo "### 5 conflict A+B (B priority 200 wins)"
M2="$G/mods/german108-high"; mkdir -p "$M2/files"
cp "$T/tests/fixtures/german108-high/manifest.json" "$M2/"
cp "$BOOT" "$M2/files/_resdesc_50_German108.lua"
run_case conflict
chk conflict "indexed|CONFLICT|winner|override:" 8
echo "### 6 invalid + disabled mods"
rm -rf "$G/mods"
mkdir -p "$G/mods/invalid-demo" "$G/mods/disabled-demo"
cp "$T/tests/fixtures/invalid-demo/manifest.json" "$G/mods/invalid-demo/"
cp "$T/tests/fixtures/disabled-demo/manifest.json" "$G/mods/disabled-demo/"
run_case invalid
chk invalid "problem:|disabled|no usable|index ready" 8
echo "### 8 M10 deps/conflicts (needs german108-override present)"
mkdir -p "$G/mods/german108-override/files"
cp "$T/tests/fixtures/german108-override/manifest.json" "$G/mods/german108-override/"
cp "$G/archives/_resdesc_50_German107.lua" "$G/mods/german108-override/files/_resdesc_50_German108.lua"
for m in dep-ok dep-missing dep-ver conflictme; do mkdir -p "$G/mods/$m/files"; done
python3 - "$G/mods" <<'EOF'
import json, sys
base = sys.argv[1]
def w(d, extra):
    m = {"id": d, "version": "1.0.0", "api": 1, "games": ["minecraft-story-mode:s1"], "files": {}}
    m.update(extra)
    json.dump(m, open(f"{base}/{d}/manifest.json", "w"), indent=4)
w("dep-ok", {"depends": [{"id": "german108.override", "version": "1.0.0"}]})
w("dep-missing", {"depends": [{"id": "no.such.mod"}]})
w("dep-ver", {"depends": [{"id": "german108.override", "version": "9.9"}]})
w("conflictme", {"conflicts": ["german108.override"]})
print("m10 fixtures written")
EOF
run_case deps
chk deps "rejected|skipped|override: german" 8
echo "### 9 M11 packaged mod (drop-in, disable, delete)"
rm -rf "$G/mods" && mkdir -p /tmp/opencode/m11m/files
cp "$T/tests/fixtures/german108-override/manifest.json" /tmp/opencode/m11m/
cp "$A107" /tmp/opencode/m11m/files/_resdesc_50_German108.lua
"$T/build/ttmod" package create /tmp/opencode/m11m /tmp/opencode/m11m.ttmod
mkdir -p "$G/mods" && cp /tmp/opencode/m11m.ttmod "$G/mods/"
run_case packaged
chk packaged "Valid mods: 1|cached from package|override: german" 5
mkdir -p "$G/config"
printf '{\n    "german108.override": {"enabled": false}\n}\n' > "$G/config/mods.json"
run_case packaged-disabled
chk packaged-disabled "Disabled: 1" 2
if grep -q "override: german" "$LOG" 2>/dev/null; then echo "FAIL: override fired while disabled"; fi
rm "$G/mods/m11m.ttmod"
ls "$G/mods/"*.ttmod 2>/dev/null || echo "(no packages left)"
run_case packaged-deleted
chk packaged-deleted "Valid mods: 0" 1
echo "### 10 M20 hybrid mod (native plugin + files override)"
rm -rf "$G/mods" && mkdir -p "$G/mods/hybrid-demo/files" "$G/mods/hybrid-demo/plugins"
cp "$T/tests/fixtures/hybrid-demo/manifest.json" "$G/mods/hybrid-demo/"
cp "$T/build-win32/example-plugins/hello-mcsm/plugin.dll" "$G/mods/hybrid-demo/plugins/hybrid.dll"
cp "$A107" "$G/mods/hybrid-demo/files/_resdesc_50_German108.lua"
run_case hybrid
chk hybrid "WARNING.*hybrid|initialized|override: hybrid" 5
echo "### 11 M24 safe mode (env; mods present but inert)"
rm -f "$G/logs/ttmod.log"
( cd "$G" && TTMOD_SAFE_MODE=1 timeout 60 wine MinecraftStoryMode.exe > "/tmp/opencode/m5_safemode.txt" 2>&1 )
echo "== safemode: exit=$? =="
grep -E "SAFE MODE|Valid mods" "$LOG" 2>/dev/null | head -n 3
if grep -q "override:\|initialized" "$LOG" 2>/dev/null; then echo "FAIL: mod code ran in safe mode"; fi
echo "### 7 resdesc override === case 4 (see valid-mod: German108 never opened)"
echo "--- originals after ---"
md5sum -c /tmp/opencode/m5_orig.md5
unstage_all
echo "--- game dir clean ---"; ls "$G" | grep -E "^(dinput8|mods|config|logs|ttmod)" || echo OK
