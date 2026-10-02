#!/bin/sh
# Verify Windows x86 runtime artifacts. Usage: tools/verify_win32.sh <build-win32-dir>
# Checks: PE32/i386, dinput8 exports DirectInput8Create undecorated,
# framework has no unexpected runtime DLL deps beyond system libs.
set -u
D="${1:?usage: verify_win32.sh <build-win32-dir>}"
fail=0
for f in "$D/dinput8.dll" "$D/ttmod_framework.dll"; do
  if ! file "$f" | grep -q "PE32.*Intel i386"; then
    echo "FAIL: $f is not PE32/i386:"; file "$f"; fail=1
  else
    echo "PASS: $f is $(file -b "$f" | cut -d, -f1-2)"
  fi
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
