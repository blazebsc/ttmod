#!/usr/bin/env python3
"""Lua 5.2 bytecode disassembler (RE aid, not a decompiler).
Usage: lua52_dis.py <chunk.luac> [max_funcs]
Layout follows stock 5.2 ldump.c exactly: header, tail, then per function:
linedefined, lastlinedefined, numparams, vararg, maxstack, code, constants,
nested protos, upvalues (instack+idx), debug (source, lineinfo, locvars,
upvalue names). Validates by exact EOF consumption.
Handles both stock size headers (int=4,size_t=8,num=8) and Telltale's
32-bit headers (4,4,4,4); non-integral numbers print as hex.
Verified byte-exact against stock luac 5.2.4 output.
"""
import struct
import sys

# 5.2 opcodes (lopcodes.h order).
OPNAMES = """MOVE LOADK LOADKX LOADBOOL LOADNIL GETUPVAL GETTABUP GETTABLE
SETTABUP SETUPVAL SETTABLE NEWTABLE SELF ADD SUB MUL DIV MOD POW UNM NOT LEN
CONCAT JMP EQ LT LE TEST TESTSET CALL TAILCALL RETURN FORLOOP FORPREP TFORCALL
TFORLOOP SETLIST CLOSURE VARARG EXTRAARG""".split()
assert len(OPNAMES) == 40

POS_A, POS_OP, POS_C, POS_B, POS_Bx, POS_Ax, POS_sBx = 6, 0, 14, 23, 14, 6, 14
SIZE_A, SIZE_OP, SIZE_C, SIZE_B, SIZE_Bx, SIZE_Ax, SIZE_sBx = 8, 6, 9, 9, 18, 26, 18
MAXARG_sBx = (1 << SIZE_sBx) - 1 >> 1


class R:
    def __init__(self, d):
        self.d = d
        self.o = 0

    def u8(self):
        v = self.d[self.o]
        self.o += 1
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.o)[0]
        self.o += 4
        return v

    def u64(self):
        v = struct.unpack_from("<Q", self.d, self.o)[0]
        self.o += 8
        return v

    def size_t(self, sz):
        if sz == 4:
            return self.u32()
        return self.u64()

    def string(self, sz):
        n = self.size_t(sz)
        if n == 0:
            return None
        s = self.d[self.o:self.o + n - 1]
        self.o += n
        return s


def dis_chunk(path, limit=1000000):
    d = open(path, "rb").read()
    assert d[:4] == b"\x1bLua", "bad signature"
    assert d[4] == 0x52, "not 5.2"
    assert d[5] == 0, "bad format"
    assert d[6] == 1, "not LE"
    isz = d[7]
    ssz = d[8]
    insz = d[9]
    numsz = d[10]
    intflag = d[11]
    assert d[12:18] == b"\x19\x93\r\n\x1a\n", "bad tail"
    assert insz == 4, "insn size"
    r = R(d)
    r.o = 18
    out = [f"# sizes int={isz} size_t={ssz} num={numsz} intflag={intflag}"]
    # NOTE: empirically (stock luac 5.2.4 + Telltale), function header is
    # source, linedefined, numparams, vararg, maxstack — NO lastlinedefined.
    count = [0]

    def func(depth):
        # Stock 5.2 layout (ldump.c): linedefined, lastlinedefined, numparams,
        # vararg, maxstack, code, consts, UPVALUES, protos, debug. Source lives
        # in the debug section, not the header.
        lined = r.u32()
        lastlined = r.u32()
        nparams = r.u8()
        vararg = r.u8()
        maxstack = r.u8()
        show = count[0] < limit
        count[0] += 1
        if show:
            out.append(f"{'  ' * depth}function line {lined}-{lastlined} "
                       f"params={nparams} vararg={vararg} maxstack={maxstack}")
        ncode = r.u32()
        code = [r.u32() for _ in range(ncode)]
        consts = []
        nk = r.u32()
        for _ in range(nk):
            t = r.u8()
            if t == 0:
                consts.append("nil")
            elif t == 1:
                consts.append("false" if r.u8() == 0 else "true")
            elif t == 3:
                if intflag:
                    consts.append(repr(r.u64() if isz == 8 else r.u32()))
                else:
                    nb = r.d[r.o:r.o + numsz]
                    r.o += numsz
                    consts.append("num:" + nb.hex())
            elif t == 4:
                s = r.string(ssz)
                consts.append(repr(s))
            else:
                raise ValueError(f"bad const tag {t} at off {r.o}")
        if show:
            def rk(v):
                return f"K{v & 0xFF}={consts[v & 0xFF][:40]}" if v & 0x100 and (v & 0xFF) < len(
                    consts) else None

            for i, ins in enumerate(code):
                op = ins & 0x3F
                a = (ins >> POS_A) & 0xFF
                b = (ins >> POS_B) & 0x1FF
                c = (ins >> POS_C) & 0x1FF
                bx = (ins >> POS_Bx) & 0x3FFFF
                sbx = bx - MAXARG_sBx
                name = OPNAMES[op] if op < len(OPNAMES) else f"OP{op}"
                if name in ("LOADK", "CLOSURE"):
                    detail = f"{a} {bx}"
                elif name in ("JMP", "FORLOOP", "FORPREP", "TFORLOOP"):
                    detail = f"{a} {sbx:+d} -> {i + 1 + sbx}"
                else:
                    detail = f"{a} {b} {c}"
                ann = []
                if name in ("LOADK", "LOADKX", "EXTRAARG") and bx < len(consts):
                    ann.append(f"K{bx}={consts[bx][:40]}")
                else:
                    if name not in ("JMP", "FORLOOP", "FORPREP", "TFORLOOP"):
                        rb, rc = rk(b), rk(c)
                        if rb:
                            ann.append(rb)
                        if rc:
                            ann.append(rc)
                if ann:
                    detail += " ; " + ", ".join(ann)
                out.append(f"{'  ' * depth}  [{i:4d}] {name:10s} {detail}")
            if consts:
                for i, cc in enumerate(consts):
                    out.append(f"{'  ' * depth}  K{i} = {cc[:100]}")
        nproto = r.u32()
        for _ in range(nproto):
            func(depth + 1)
        nupvals = r.u32()
        r.o += nupvals * 2  # instack + idx per upvalue
        # debug info: source, lineinfo, locvars, upvalue names
        src = r.string(ssz)
        nline = r.u32()
        r.o += nline * 4
        nloc = r.u32()
        for _ in range(nloc):
            r.string(ssz)
            r.u32()
            r.u32()
        nup = r.u32()
        for _ in range(nup):
            r.string(ssz)

    func(0)
    out.append(f"# consumed {r.o}/{len(d)} bytes, {count[0]} functions")
    assert r.o == len(d), f"TRAILING BYTES: {len(d) - r.o}"
    return "\n".join(out)


if __name__ == "__main__":
    lim = int(sys.argv[2]) if len(sys.argv) > 2 else 1000000
    print(dis_chunk(sys.argv[1], lim))
