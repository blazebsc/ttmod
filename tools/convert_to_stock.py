#!/usr/bin/env python3
"""Convert Telltale 32-bit Lua 5.2 chunks to stock 5.2.4-loadable chunks.
Same code/strings/structure; only widths change: 4-byte Telltale floats ->
8-byte doubles, size_t 4 -> 8. Widening is lossless. Purpose: execute
RE’d game scripts in a stock VM with stubbed globals to validate
control flow and call shapes (NOT game behavior).
Usage: convert_to_stock.py in.dec.lua out.luac
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
from lua52_edit import parse_chunk


def convert(path, outpath):
    meta, f = parse_chunk(path)
    isz, ssz, numsz, intflag = meta
    assert (isz, ssz, intflag) == (4, 4, 0), meta

    def conv_func(p):
        for c in p.consts:
            if c.tag == 3:
                (f32,) = struct.unpack("<f", c.val[1])
                c.val = ("f", struct.pack("<d", f32))
        for q in p.protos:
            conv_func(q)

    conv_func(f)
    # re-emit with stock widths via lua52_edit serializer knobs: emulate by
    # temporarily rewriting sizes — simplest: build output manually here.
    import lua52_edit as E

    w = bytearray(b"\x1bLua\x52\x00\x01\x04\x08\x04\x08\x00\x19\x93\r\n\x1a\n")

    def wu32(v):
        w.extend(struct.pack("<I", v & 0xFFFFFFFF))

    def wu64(v):
        w.extend(struct.pack("<Q", v & 0xFFFFFFFFFFFFFFFF))

    def wstr(b):
        wu64(len(b))
        w.extend(b)

    def wfunc(p):
        wu32(p.lined)
        wu32(p.lastlined)
        w.extend(bytes([p.nparams, p.vararg, p.maxstack]))
        wu32(len(p.code))
        for ins in p.code:
            wu32(ins)
        wu32(len(p.consts))
        for c in p.consts:
            w.extend(bytes([c.tag]))
            if c.tag == 0:
                pass
            elif c.tag == 1:
                w.extend(bytes([c.val]))
            elif c.tag == 3:
                assert c.val[0] == "f"
                w.extend(c.val[1])
            elif c.tag == 4:
                wstr(c.val)
            else:
                raise ValueError("tag")
        wu32(len(p.protos))
        for q in p.protos:
            wfunc(q)
        wu32(len(p.upvals))
        for (a, b) in p.upvals:
            w.extend(bytes([a, b]))
        if p.src is None:
            wu64(0)
        else:
            wstr(p.src)
        wu32(len(p.lineinfo))
        for v in p.lineinfo:
            wu32(v)
        wu32(len(p.locvars))
        for (nm, s, e) in p.locvars:
            wstr(nm)
            wu32(s)
            wu32(e)
        wu32(len(p.upnames))
        for nm in p.upnames:
            wstr(nm)

    wfunc(f)
    open(outpath, "wb").write(bytes(w))
    print(f"converted {len(w)} bytes -> {outpath}")


convert(sys.argv[1], sys.argv[2])
