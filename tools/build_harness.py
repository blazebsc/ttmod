#!/usr/bin/env python3
"""Harness: wrap the REAL (converted) Menu_Main button function in a stock
5.2 chunk that binds its upvalues to stubs, then execute it in stock lua 5.2.
Proves the inserted Mods Menu_Add block executes with exact expected args.

Usage: build_harness.py <mods.dec.lua> <harness.luac>
Then:  lua5.2 harness_runner.lua harness.luac   (runner asserts + prints calls)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
from lua52_dis import OPNAMES
from lua52_edit import parse_chunk, Func, Const, enc_ins


def build(src, dst):
    meta, f = parse_chunk(src)
    isz, ssz, numsz, intflag = meta
    assert (isz, ssz, intflag) == (4, 4, 0), meta
    tgt = f.protos[16].protos[1]
    assert tgt.lined == 397, tgt.lined

    def widen(p):
        for c in p.consts:
            if c.tag == 3 and c.val[0] == "f" and len(c.val[1]) == 4:
                (f32,) = struct.unpack("<f", c.val[1])
                c.val = ("f", struct.pack("<d", f32))
        for q in p.protos:
            widen(q)

    widen(tgt)
    # rewire upvalue descriptors to stack slots 0..3 (harness provides them)
    assert len(tgt.upvals) == 4, tgt.upvals
    tgt.upvals = [(1, 0), (1, 1), (1, 2), (1, 3)]
    # quitfn proto: return 'Quit'
    q = Func()
    q.lined, q.lastlined, q.nparams, q.vararg, q.maxstack = 0, 0, 0, 0, 2
    q.consts = [Const(4, b"Quit\x00")]
    q.code = [enc_ins("LOADK", a=0, bx=0), enc_ins("RETURN", a=0, b=2, c=0)]
    q.lineinfo = [0, 0]
    # harness main: R0=_ENV, R1=false, R2=8.0, R3=quitfn; CLOSURE target; call it
    m = Func()
    m.lined, m.lastlined, m.nparams, m.vararg, m.maxstack = 0, 0, 0, 0, 6
    m.consts = [Const(3, ("f", struct.pack("<d", 8.0)))]
    m.code = [
        enc_ins("GETUPVAL", a=0, b=0, c=0),
        enc_ins("LOADBOOL", a=1, b=0, c=0),
        enc_ins("LOADK", a=2, bx=0),
        enc_ins("CLOSURE", a=3, bx=1),
        enc_ins("CLOSURE", a=4, bx=0),
        enc_ins("NEWTABLE", a=5, b=0, c=0),
        enc_ins("CALL", a=4, b=2, c=1),
        enc_ins("RETURN", a=0, b=1, c=0),
    ]
    m.protos = [tgt, q]
    m.upvals = [(0, 0)]
    m.upnames = [b"_ENV\x00"]
    m.lineinfo = [0] * len(m.code)
    # emit stock widths
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
                if len(c.val[1]) == 4:
                    (f32,) = struct.unpack("<f", c.val[1])
                    w.extend(struct.pack("<d", f32))
                else:
                    assert len(c.val[1]) == 8
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

    wfunc(m)
    open(dst, "wb").write(bytes(w))
    print(f"harness {len(w)} bytes -> {dst}")


build(sys.argv[1], sys.argv[2])
