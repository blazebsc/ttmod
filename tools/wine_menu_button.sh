#!/bin/sh
# Dedicated Milestone-A UI validation (separate from the fast matrix: the main
# menu only loads in long sessions). Builds the menu-button mod locally,
# stages it, then loops launches until Menu streams open or tries run out.
# Usage: TOOLKIT_DIR=... GAME_DIR=... TTK_DATA=... sh tools/wine_menu_button.sh [max_tries]
# Never modifies original game files (md5-checked); cleans staged files after.
set -u
T="$(dirname "$0")/.."
: "${GAME_DIR:?set GAME_DIR}"
: "${TOOLKIT_DIR:?set TOOLKIT_DIR (TelltaleToolKit checkout with writer fix)}"
: "${TTK_DATA:=$TOOLKIT_DIR/data}"
MAXT="${1:-25}"
G="$GAME_DIR"
A108="$G/archives/_resdesc_50_German108.lua"
md5sum "$G/archives/MCSM_pc_Menu_data.ttarch2" | tee /tmp/opencode/menu_orig.md5
sh "$T/tools/build_menu_button.sh" /tmp/opencode/menumod_out || exit 1
cp "$T/build-win32/dinput8.dll" "$T/build-win32/ttmod_framework.dll" "$G/" || exit 1
rm -rf "$G/mods" && mkdir -p "$G/mods"
cp -r /tmp/opencode/menumod_out/ttmod-menu-test "$G/mods/"
for i in $(seq 1 "$MAXT"); do
  rm -f "$G/logs/ttmod.log"
  START=$(date +%s)
  ( cd "$G" && timeout 300 wine MinecraftStoryMode.exe > "/tmp/opencode/mB_ui_$i.txt" 2>&1 )
  dur=$(( $(date +%s) - START ))
  menu=$(grep -c "Menu_ms\|Menu_txmesh" "$G/logs/ttmod.log" 2>/dev/null || true)
  ovr=$(grep -c "ttmod.menu.test" "$G/logs/ttmod.log" 2>/dev/null || true)
  echo "try$i dur=${dur}s menu=$menu override_lines=$ovr"
  if [ "$dur" -gt 90 ]; then echo "LONG SESSION try $i — inspect manually"; break; fi
  sleep 2
done
echo "--- original intact ---"
md5sum -c /tmp/opencode/menu_orig.md5
rm -rf "$G/mods" "$G/dinput8.dll" "$G/ttmod_framework.dll"
rm -rf "$G/config" "$G/logs" "$G/ttmod"
echo "--- game dir clean ---"
ls "$G" | grep -E "^(dinput8|mods|config|logs|ttmod)$" || echo OK
