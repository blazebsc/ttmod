#!/bin/sh
# Verify Windows x86 runtime artifacts. Usage: tools/verify_win32.sh <build-win32-dir>
# Checks: PE32/i386, dinput8 exports DirectInput8Create undecorated,
# framework has no unexpected runtime DLL deps beyond system libs.
set -u
D="${1:?usage: verify_win32.sh <build-win32-dir>}"
fail=0
for f in "$D/dinput8.dll" "$D/ttmod_framework.dll"; do
  # `file` renders the machine type differently depending on the toolchain that
  # built it: the nix mingw says "Intel i386", apt mingw says "Intel 80386".
  # Both are PE32 with machine type 0x014C. Matching only one spelling failed
  # every CI run while the artifacts were perfectly fine, so accept either - and
  # fail loudly on the two things that actually matter (32-bit PE, or x64).
  desc=$(file -b "$f")
  case "$desc" in
    *PE32*Intel\ i386*|*PE32*Intel\ 80386*)
      echo "PASS: $f is $(echo "$desc" | cut -d, -f1-2)" ;;
    *)
      echo "FAIL: $f is not PE32/i386:"; echo "$desc"; fail=1 ;;
  esac
done
if ! objdump -p "$D/dinput8.dll" | grep -q "DirectInput8Create$"; then
  echo "FAIL: dinput8.dll missing undecorated DirectInput8Create export"
  objdump -p "$D/dinput8.dll" | sed -n '/Ordinal\/Name Pointer/,+4p'
  fail=1
else
  echo "PASS: dinput8.dll exports undecorated DirectInput8Create"
fi
echo "--- framework DLL deps ---"
objdump -p "$D/ttmod_framework.dll" | grep "DLL Name"
if objdump -p "$D/ttmod_framework.dll" | grep -i -E "libstdc|libgcc|winpthread"; then
  echo "FAIL: framework depends on MinGW runtime DLLs (ship or -static)"
  fail=1
else
  echo "PASS: framework needs only system DLLs"
fi
[ "$fail" -eq 0 ] && echo "ALL CHECKS PASSED" || echo "CHECKS FAILED"
exit "$fail"
