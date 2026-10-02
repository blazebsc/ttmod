#!/bin/sh
# Build a clean TTMod release. Usage: sh tools/make_release.sh [outdir]
# Produces <outdir>/TTMod-<version>/ with framework DLLs, example .ttmod
# packages (built from source, no game content), README.txt, LICENSE.txt.
# Verifies PE arch + package validity. Never touches game files.
set -u
T="$(dirname "$0")/.."
OUT="${1:-$T/release}"
VER=$(grep -m1 'project(ttmod VERSION' "$T/CMakeLists.txt" | sed 's/.*VERSION \([0-9.]*\).*/\1/')
R="$OUT/TTMod-$VER"
echo "TTMod release $VER -> $R"
rm -rf "$R"
mkdir -p "$R/examples"
cp "$T/build-win32/dinput8.dll" "$T/build-win32/ttmod_framework.dll" "$R/" || exit 1
# Example plugins (code only, no game content)
for ex in hello-mcsm title-mcsm event-log; do
  rm -rf "/tmp/opencode/rel_$ex"
  mkdir -p "/tmp/opencode/rel_$ex"
  cp "$T/examples/$ex/manifest.json" "/tmp/opencode/rel_$ex/"
  cp "$T/build-win32/example-plugins/$ex/plugin.dll" "/tmp/opencode/rel_$ex/plugin.dll"
  "$T/build/ttmod" package create "/tmp/opencode/rel_$ex" "$R/examples/$ex.ttmod" || exit 1
  "$T/build/ttmod" package validate "$R/examples/$ex.ttmod" || exit 1
done
# Resource-mod template (manifest + instructions; user adds payload files)
mkdir -p "$R/examples/resource-template/files"
cp "$T/examples/mods/german108-override/manifest.json" "$R/examples/resource-template/manifest.json"
cat > "$R/examples/resource-template/README.txt" <<'EOF'
Example resource mod template.
1. Copy a game file you want to replace into files/ (same relative path as
   listed in manifest.json), e.g. files/archives/_resdesc_50_German108.lua
2. Edit manifest.json id/name as needed.
3. Either drop this folder into the game's mods/ (development) or package it:
     ttmod package create resource-template MyMod.ttmod
   and drop MyMod.ttmod into mods/.
Only replace files you understand; keep the original game files untouched
(they stay untouched - overrides live in your mod).
EOF
cp "$T/LICENSE" "$R/LICENSE.txt"
cp "$T/docs/third-party.md" "$R/THIRD-PARTY.txt"
cat > "$R/README.txt" <<EOF
TTMod $VER - native modding framework for Minecraft: Story Mode (Season 1).

INSTALL
1. Copy dinput8.dll and ttmod_framework.dll next to MinecraftStoryMode.exe.
2. Launch the game once (creates mods/, config/, logs/).
3. Drop .ttmod files from examples/ (or your own) into mods/.
4. Launch the game. See logs/ttmod.log to confirm mods loaded.

UNINSTALL: delete the two DLLs (plus mods/, config/, logs/, ttmod/ if wanted).
Game files are never modified.

SAFETY: mods with native plugins log a WARNING on load and can run arbitrary
code - only use mods you trust. Trouble? Set TTMOD_SAFE_MODE=1 or create an
empty file config/safe-mode to launch with all mods disabled.

Docs: https://github.com/anomalyco/opencode (Meta Muse Spark build)
Full documentation ships with the source tree under docs/.
EOF
echo "--- verify ---"
file "$R/dinput8.dll" "$R/ttmod_framework.dll" | sed 's/^/  /'
ls "$R/examples"
echo "OK: $R"
