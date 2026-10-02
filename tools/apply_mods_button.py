#!/usr/bin/env python3
"""Apply the Mods-button prototype edit to decrypted Menu_Main.lua bytecode.
Transformation only - no game content. Input: decrypted chunk (with stock
\\x1bLua header). Output: modified chunk, same format.

Edit (all verified against real const tables, see docs):
- function main.protos[16].protos[1] (Menu_Main line 397, the main button list)
- append consts 'mods', 'Menu_Mods()' (own identities; label reuses the
  known-good 'label_help' key so the row renders real text while the
  landb/dlog write path is unbuilt — temporary evidence plumbing)
- insert before final RETURN, using scratch regs R2-R6 (dead at that point):
    GETTABUP R2 _ENV.Menu_Add | GETTABUP R3 _ENV.ListButton
    LOADK R4 'mods' | LOADK R5 'label_help'
    LOADK R6 'Menu_Mods()' (framework-registered via the Lua bridge)
    CALL R2 (4 args, 0 results)
Crossed relative jumps are fixed automatically; new const appended (no
existing index shifts); maxstack untouched (regs < 11).
Usage: apply_mods_button.py in.dec.lua out.dec.lua
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
from lua52_edit import parse_chunk, serialize, append_const_str, insert_code, enc_ins


def Kstr(func, idx):
    c = func.consts[idx]
    assert c.tag == 4, (idx, c)
    return c.val[:-1]


def main():
    src, dst = sys.argv[1], sys.argv[2]
    meta, f = parse_chunk(src)
    tgt = f.protos[16].protos[1]
    assert tgt.lined == 397, tgt.lined
    # anchor constants must match the known build
    assert Kstr(tgt, 1) == b"Menu_Add", Kstr(tgt, 1)
    assert Kstr(tgt, 6) == b"ListButton", Kstr(tgt, 6)
    assert Kstr(tgt, 68) == b"label_help", Kstr(tgt, 68)
    # final instruction must be the function RETURN we insert before
    last = tgt.code[-1]
    assert (last & 0x3F) == 31, hex(last)  # RETURN
    idx_mods = append_const_str(tgt, b"mods")
    idx_cb = append_const_str(tgt, b"Menu_Mods()")
    pos = len(tgt.code) - 1
    # NOTE: LOADK Bx is a DIRECT const index (no RK flag bit); only B/C
    # 9-bit operands (GETTABUP etc.) set 0x100 for constants.
    new_ins = [
        enc_ins("GETTABUP", a=2, b=0, c=257),
        enc_ins("GETTABUP", a=3, b=0, c=262),
        enc_ins("LOADK", a=4, bx=idx_mods),
        enc_ins("LOADK", a=5, bx=68),
        enc_ins("LOADK", a=6, bx=idx_cb),
        enc_ins("CALL", a=2, b=5, c=1),
    ]
    insert_code(tgt, pos, new_ins)
    open(dst, "wb").write(serialize(meta, f))
    print(f"mods-button edit applied: +2 consts, +6 insns -> {dst}")


main()
