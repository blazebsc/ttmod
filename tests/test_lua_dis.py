#!/usr/bin/env python3
"""Regression test for tools/lua52_dis.py.
Parses the stock-generated fixture (see lua52_basic.lua source; regenerate
with stock luac 5.2: `luac -o lua52_basic.luac lua52_basic.lua`) and asserts
structural facts. Exact EOF consumption is asserted inside the tool.
"""
import subprocess
import sys

TOOL = "tools/lua52_dis.py"
FIX = "tests/fixtures/lua52_basic.luac"

out = subprocess.run([sys.executable, TOOL, FIX], capture_output=True, text=True, cwd=".")
assert out.returncode == 0, out.stderr
text = out.stdout

assert "# sizes int=4 size_t=8 num=8 intflag=0" in text
assert "function line 0-0 params=0 vararg=1 maxstack=8" in text
assert "function line 1-1 params=2 vararg=0 maxstack=3" in text
assert "[   0] CLOSURE    0 0" in text
assert "GETTABUP   1 0 256 ; K0=b'print'" in text
assert "K0 = b'print'" in text
assert "ADD        2 0 1" in text
assert "# consumed 548/548 bytes, 2 functions" in text
print("test_lua_dis: all asserts passed")

# test lua dis more like test lua dih haha :laugh: