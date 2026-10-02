#!/usr/bin/env python3
"""Lua 5.2 bytecode editor: parse -> mutate -> serialize.
Edit model: append constants, insert instructions (with automatic fixing of
relative jumps crossing the insertion point), bump maxstack. No other
mutations supported (keeps the problem small and reviewable).

Jump fix rule for inserting N instructions at index P:
  for each jump-type insn at index i with target t:
    if i < P <= t: t += N   (forward jump over the hole)
    elif t < P <= i: t -= N (backward jump over the hole)
Lineinfo: insert copies of the preceding entry. Locvars: startpc/endpc
adjusted by the same rule as jump endpoints.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
from lua52_dis import R, OPNAMES, POS_A, POS_OP, POS_C, POS_B, POS_Bx, POS_sBx, MAXARG_sBx

JUMPS = {"JMP", "FORLOOP", "FORPREP", "TFORLOOP"}


class Const:
    def __init__(self, tag, val):
        self.tag = tag  # 0 nil, 1 bool, 3 num(raw bytes), 4 str(bytes incl NUL? no: raw)
        self.val = val

    def __repr__(self):
        return f"C({self.tag},{self.val!r})"


class Func:
    def __init__(self):
        self.lined = 0
        self.lastlined = 0
        self.nparams = 0
        self.vararg = 0
        self.maxstack = 0
        self.code = []
        self.consts = []  # Const
        self.protos = []  # Func
        self.upvals = []  # (instack, idx)
        self.src = None
        self.lineinfo = []
        self.locvars = []  # (name, startpc, endpc)
        self.upnames = []


def parse_func(r, isz, ssz, numsz, intflag):
    f = Func()
    f.lined = r.u32()
    f.lastlined = r.u32()
    f.nparams = r.u8()
    f.vararg = r.u8()
    f.maxstack = r.u8()
    ncode = r.u32()
    f.code = [r.u32() for _ in range(ncode)]
    nk = r.u32()
    for _ in range(nk):
        t = r.u8()
        if t == 0:
            f.consts.append(Const(0, None))
        elif t == 1:
            f.consts.append(Const(1, r.u8()))
        elif t == 3:
            if intflag:
                f.consts.append(Const(3, ("i", r.u64() if isz == 8 else r.u32())))
            else:
                f.consts.append(Const(3, ("f", r.d[r.o:r.o + numsz])))
                r.o += numsz
        elif t == 4:
            n = r.size_t(ssz)
            assert n > 0
            f.consts.append(Const(4, r.d[r.o:r.o + n]))
            r.o += n
        else:
            raise ValueError(f"bad const tag {t}")
    nproto = r.u32()
    for _ in range(nproto):
        f.protos.append(parse_func(r, isz, ssz, numsz, intflag))
    nup = r.u32()
    for _ in range(nup):
        f.upvals.append((r.u8(), r.u8()))
    n = r.size_t(ssz)
    if n == 0:
        f.src = None
    else:
        f.src = r.d[r.o:r.o + n]
        r.o += n
    nline = r.u32()
    f.lineinfo = [r.u32() for _ in range(nline)]
    nloc = r.u32()
    for _ in range(nloc):
        n2 = r.size_t(ssz)
        assert n2 > 0
        nm = r.d[r.o:r.o + n2]
        r.o += n2
        f.locvars.append((nm, r.u32(), r.u32()))
    nun = r.u32()
    for _ in range(nun):
        n2 = r.size_t(ssz)
        assert n2 > 0
        f.upnames.append(r.d[r.o:r.o + n2])
        r.o += n2
    return f


HDR = None


def parse_chunk(path):
    global HDR
    d = open(path, "rb").read()
    assert d[:4] == b"\x1bLua" and d[4] == 0x52 and d[6] == 1
    isz, ssz, insz, numsz, intflag = d[7], d[8], d[9], d[10], d[11]
    assert d[12:18] == b"\x19\x93\r\n\x1a\n" and insz == 4
    r = R(d)
    r.o = 18
    f = parse_func(r, isz, ssz, numsz, intflag)
    assert r.o == len(d), f"trailing {len(d) - r.o}"
    HDR = d[:18]
    return (isz, ssz, numsz, intflag), f


def enc_ins(op, a=0, b=0, c=0, bx=None, sbx=None):
    i = OPNAMES.index(op)
    if bx is not None:
        return i | (a << 6) | (bx << 14)
    if sbx is not None:
        return i | (a << 6) | ((sbx + MAXARG_sBx) << 14)
    return i | (a << 6) | (c << 14) | (b << 23)


def jump_target(i, ins):
    op = OPNAMES[ins & 0x3F]
    if op not in JUMPS:
        return None
    sbx = ((ins >> 14) & 0x3FFFF) - MAXARG_sBx
    return i + 1 + sbx


def set_jump_target(ins, t, i):
    op = OPNAMES[ins & 0x3F]
    a = (ins >> 6) & 0xFF
    sbx = t - i - 1
    return (ins & 0x3F) | (a << 6) | ((sbx + MAXARG_sBx) << 14)


def insert_code(f, pos, insns, line=None):
    """Insert instructions at pos; fix crossed jumps, lineinfo, locvars."""
    n = len(insns)
    assert 0 <= pos <= len(f.code)
    # fix existing jumps
    for idx in range(len(f.code)):
        t = jump_target(idx, f.code[idx])
        if t is None:
            continue
        if idx < pos <= t:
            f.code[idx] = set_jump_target(f.code[idx], t + n, idx)
        elif t < pos <= idx:
            f.code[idx] = set_jump_target(f.code[idx], t - n, idx)
    f.code[pos:pos] = insns
    # lineinfo: replicate preceding entry (debug-only)
    fill = f.lineinfo[pos - 1] if 0 < pos <= len(f.lineinfo) else (f.lineinfo[-1] if f.lineinfo else 0)
    f.lineinfo[pos:pos] = [fill] * n
    # locvars crossing the hole
    fixed = []
    for (nm, s, e) in f.locvars:
        ns = s + n if s >= pos else s
        ne = e + n if e > pos else e
        fixed.append((nm, ns, ne))
    f.locvars = fixed
    # maxstack bump is caller's job


def append_const_str(f, s: bytes):
    """Append string const (with NUL); return index."""
    f.consts.append(Const(4, s + b"\x00"))
    return len(f.consts) - 1


def write_u32(w, v):
    w.extend(struct.pack("<I", v & 0xFFFFFFFF))


def write_func(w, f, isz, ssz, numsz, intflag):
    write_u32(w, f.lined)
    write_u32(w, f.lastlined)
    w.extend(bytes([f.nparams, f.vararg, f.maxstack]))
    write_u32(w, len(f.code))
    for ins in f.code:
        write_u32(w, ins)
    write_u32(w, len(f.consts))
    for c in f.consts:
        w.extend(bytes([c.tag]))
        if c.tag == 0:
            pass
        elif c.tag == 1:
            w.extend(bytes([c.val]))
        elif c.tag == 3:
            if c.val[0] == "i":
                if isz == 8:
                    w.extend(struct.pack("<Q", c.val[1]))
                else:
                    w.extend(struct.pack("<I", c.val[1]))
            else:
                w.extend(c.val[1])
        elif c.tag == 4:
            n = len(c.val)
            if ssz == 4:
                write_u32(w, n)
            else:
                w.extend(struct.pack("<Q", n))
            w.extend(c.val)
        else:
            raise ValueError("bad tag")
    write_u32(w, len(f.protos))
    for p in f.protos:
        write_func(w, p, isz, ssz, numsz, intflag)
    write_u32(w, len(f.upvals))
    for (a, b) in f.upvals:
        w.extend(bytes([a, b]))
    if f.src is None:
        if ssz == 4:
            write_u32(w, 0)
        else:
            w.extend(struct.pack("<Q", 0))
    else:
        if ssz == 4:
            write_u32(w, len(f.src))
        else:
            w.extend(struct.pack("<Q", len(f.src)))
        w.extend(f.src)
    write_u32(w, len(f.lineinfo))
    for v in f.lineinfo:
        write_u32(w, v)
    write_u32(w, len(f.locvars))
    for (nm, s, e) in f.locvars:
        if ssz == 4:
            write_u32(w, len(nm))
        else:
            w.extend(struct.pack("<Q", len(nm)))
        w.extend(nm)
        write_u32(w, s)
        write_u32(w, e)
    write_u32(w, len(f.upnames))
    for nm in f.upnames:
        if ssz == 4:
            write_u32(w, len(nm))
        else:
            w.extend(struct.pack("<Q", len(nm)))
        w.extend(nm)


def serialize(meta, f):
    isz, ssz, numsz, intflag = meta
    w = bytearray(HDR)
    write_func(w, f, isz, ssz, numsz, intflag)
    return bytes(w)


if __name__ == "__main__":
    import sys

    if sys.argv[1] == "roundtrip":
        meta, f = parse_chunk(sys.argv[2])
        out = serialize(meta, f)
        orig = open(sys.argv[2], "rb").read()
        print(f"orig={len(orig)} new={len(out)} identical={out == orig}")
