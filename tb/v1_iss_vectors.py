#!/usr/bin/env python3
"""Test vectors for the golden ISS (tb/sh4_iss.h, unit C of rtl/v1/ARCH.md), checked by tb/v1_iss_tb.cpp.

Three independent references, one file each in the output directory (default build/v1_iss):

decode.txt   isa.py's decode: for every 16-bit code and each FPU mode M0 (PR 0 SZ 0), M1 (PR 0 SZ 1), M2 (PR 1 SZ 0)
             the isa.py form it matches (FORMS_SRC fixed bits), or none.  isa.py excludes SLEEP (H'001B) and TRAPA
             (H'C3xx); they are added here, as the ISS decodes them.
             Format: "N <count>" + one "F <idx> <name>" per form, then 3 x 65536 form indices (-1 = illegal),
             16 per line, M0 first.

int.txt      isa.py's functional model (execute()) on random states, for every form in every mode it exists in.
             The pre-state is NOT written out: it is a pure function of a 64-bit seed (gen_state(), mirrored in
             v1_iss_tb.cpp; a checksum guards the two copies), plus a few overrides (address registers, T, FPSCR mode,
             MD).  Memory bytes are hash(seed, physical address) except the instruction words.
             Line: "V <form> <mode> <seed> <op> <slot> <pc> <nsteps> <npc> <ck> o.<key>=<hex>... e.<key>=<hex|?>...
             w.<physaddr>=<byte|?>..."  -- o = override, e = a register the instruction writes (? = isa.py does not
             know its value: FP arithmetic, MOVCA / OCBI line contents), w = every byte the instruction stores.
             Every register not listed under e. must be unchanged; the stores must be exactly the w. set.
             What isa.py does not model is added here, each rule from the manual:
               * SR = value & 0x700083F3 (LDC / LDC.L / RTE) and the R0-R7 bank swap when MD & RB changes;
               * FPSCR = value & 0x003FFFFF (LDS / LDS.L); FRCHG / FSCHG write FPSCR = isa.py's fpscr_mode;
               * MAC.W / MAC.L values: a literal Python port of the manual's C code (pm.txt 10.53 / 10.54, the
                 MAC.L sign-extension typo read as intended, MACH bit 0 on a MAC.W S = 1 overflow as its text says);
             and what isa.py models differently from the silicon is avoided: MAC with n == m (isa.py advances the
             register once, the manual twice -- tested directly in the tb), FCMP operands that are NaN or denormal
             (isa.py compares raw bit patterns; FCMP DR also only the high words, so the low words are zero).

fp.txt       exact-arithmetic reference (Python integers, no host FPU) for every FP operation of the ISS in RN and
             RZ: "<op> <rz> <a> <b> <c> <result> <cause>" (hex; cause bits I 1 U 2 O 4 Z 8 V 16).  The rules are the
             ISS's (sh4_iss.h namespace sh4iss::fp): DN = 1, SH-4 NaN convention, IEEE rounding with gradual
             underflow then flush of a denormal result (U | I), overflow per IEEE (O | I), fused FMAC, FTRC
             saturation.  Operands: random bit patterns, specials, short mantissas (exact / tie results), exponent
             sums at the overflow / underflow edges, cancellation, near-ties.

usage: v1_iss_vectors.py [out_dir] [--int N] [--fp N] [--jobs J]   (N = vectors per form / per op and mode)
"""
import argparse
import multiprocessing
import os
import random
import sys
from fractions import Fraction
from math import isqrt

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'model', 'timings2'))
import isa      # noqa: E402

M32 = 0xFFFFFFFF
M64 = (1 << 64) - 1
SR_MASK, FPSCR_MASK, PHYS = 0x700083F3, 0x003FFFFF, 0x1FFFFFFF


# -------------------------------------------------------------------------------------------------------------------
# Deterministic state generator (mirrored in v1_iss_tb.cpp: keep the two identical)
def mix64(z):
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
    return z ^ (z >> 31)


class Rng:
    """splitmix64"""

    def __init__(self, seed):
        self.s = seed & M64

    def next(self):
        self.s = (self.s + 0x9E3779B97F4A7C15) & M64
        return mix64(self.s)


EDGE = [0, 1, 2, 0x7F, 0x80, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000, 0x80000001,
        0xFFFFFFFF, 0xFFFFFFFE, 0xFFFF8000, 0xFFFFFF80, 0x55555555, 0xAAAAAAAA, 31, 32, 0xFFFFFFE0, 0xFFFFFFE1]
FEDGE = [0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x7F800000, 0xFF800000, 0x7FBFFFFF, 0x7F800001,
         0x7FC00000, 0xFFFFFFFF, 0x00000001, 0x807FFFFF, 0x00800000, 0x7F7FFFFF, 0x40000000, 0x3F000000]


def word(r):
    x = r.next()
    c, y = x & 7, (x >> 32) & M32
    if c <= 2:
        return y
    if c <= 4:
        return EDGE[y % len(EDGE)]
    if c == 5:
        return ((y & 0xFF) - 128) & M32
    if c == 6:
        return (y & 0x3F) | (y & 0x80000000)
    k, sel = y & 31, (y >> 5) % 3
    return [1 << k, (1 << k) - 1, (-(1 << k)) & M32][sel]


def fpword(r):
    x = r.next()
    c, y = x & 3, (x >> 32) & M32
    if c == 0:
        return y
    if c == 1:
        return FEDGE[y % len(FEDGE)]
    return (y & 0x807FFFFF) | ((((y >> 23) & 0x3F) + 100) << 23)


SPECIAL_KEYS = ['gbr', 'vbr', 'ssr', 'spc', 'sgr', 'dbr', 'mach', 'macl', 'pr', 'fpul']


def gen_state(seed):
    """seed -> {key: value}; keys r0..r15 b0..b7 sr gbr vbr ssr spc sgr dbr mach macl pr fpul fpscr fa0.. fb0.."""
    r = Rng(seed)
    st = {}
    for k in range(16):
        st[f'r{k}'] = word(r)
    for k in range(8):
        st[f'b{k}'] = word(r)
    st['sr'] = word(r) & SR_MASK & ~0x8000            # FD = 0
    for nm in SPECIAL_KEYS:
        st[nm] = word(r)
    st['fpscr'] = word(r) & FPSCR_MASK
    for b in 'ab':
        for k in range(16):
            st[f'f{b}{k}'] = fpword(r)
    return st


STATE_KEYS = [f'r{k}' for k in range(16)] + [f'b{k}' for k in range(8)] + ['sr'] + SPECIAL_KEYS + ['fpscr'] + \
             [f'fa{k}' for k in range(16)] + [f'fb{k}' for k in range(16)]


def checksum(st):
    c = 0
    for k in STATE_KEYS:
        c = mix64((c ^ st[k]) & M64)
    return c


def memb(seed, a):
    """initial memory byte at physical address a"""
    return mix64((seed ^ ((a & PHYS) * 0x9E3779B97F4A7C15)) & M64) & 0xFF


# -------------------------------------------------------------------------------------------------------------------
# decode table
def base_name(nm):
    return nm.replace(' (taken)', '').replace(' (not taken)', '')


def iss_names():
    """the ISS's form names in its order: isa.py order, merged branch variants, then SLEEP, TRAPA"""
    out = []
    for f in isa.FORMS:
        nm = base_name(f.name)
        if nm not in out:
            out.append(nm)
    return out + ['SLEEP', 'TRAPA #imm']


def decode_table(names):
    idx = {n: i for i, n in enumerate(names)}
    tabs = []
    for mode in ('M0', 'M1', 'M2'):
        by_mask = {}
        for f in isa.forms_in(mode):
            by_mask.setdefault(f.fixed_mask, {})[f.fixed_bits] = idx[base_name(f.name)]
        t = []
        for op in range(65536):
            hit = -1
            for mask, d in by_mask.items():
                v = d.get(op & mask)
                if v is not None:
                    assert hit in (-1, v), (mode, hex(op))
                    hit = v
            if op == 0x001B:
                hit = idx['SLEEP']
            if op >> 8 == 0xC3:
                hit = idx['TRAPA #imm']
            t.append(hit)
        tabs.append(t)
    return tabs


# -------------------------------------------------------------------------------------------------------------------
# isa.py vectors
class RecState(isa.State):
    """isa.State that logs its stores"""

    def __init__(self):
        super().__init__()
        self.log = []

    def st(self, addr, size, v):
        self.log.append((addr, size, v))
        super().st(addr, size, v)


PRIV = {'LDC Rm,SR', 'LDC Rm,VBR', 'LDC Rm,SSR', 'LDC Rm,SPC', 'LDC Rm,DBR', 'LDC Rm,Rn_BANK', 'LDC.L @Rm+,SR',
        'LDC.L @Rm+,VBR', 'LDC.L @Rm+,SSR', 'LDC.L @Rm+,SPC', 'LDC.L @Rm+,DBR', 'LDC.L @Rm+,Rn_BANK', 'STC SR,Rn',
        'STC VBR,Rn', 'STC SSR,Rn', 'STC SPC,Rn', 'STC SGR,Rn', 'STC DBR,Rn', 'STC Rm_BANK,Rn', 'STC.L SR,@-Rn',
        'STC.L VBR,@-Rn', 'STC.L SSR,@-Rn', 'STC.L SPC,@-Rn', 'STC.L SGR,@-Rn', 'STC.L DBR,@-Rn',
        'STC.L Rm_BANK,@-Rn', 'LDTLB', 'RTE'}

ISA_OF_KEY = {**{f'r{k}': f'r{k}' for k in range(16)}, **{f'b{k}': f'bk{k}' for k in range(8)},
              **{k: k for k in SPECIAL_KEYS + ['fpscr']},
              **{f'fa{k}': f'fa{k}' for k in range(16)}, **{f'fb{k}': f'fb{k}' for k in range(16)}}
KEY_OF_ISA = {v: k for k, v in ISA_OF_KEY.items()}


def macw_manual(mach, macl, wn, wm, S):
    """pm.txt 10.54 MACW(), literally (+ MACH bit 0 on an S = 1 overflow, from the Description)"""
    def s32(v):
        return isa.s32(v)
    tempn, tempm = isa.sx16(wn), isa.sx16(wm)
    templ = macl
    tempm = s32(tempn * tempm)
    dest = 0 if s32(macl) >= 0 else 1
    if tempm >= 0:
        src, tempn = 0, 0
    else:
        src, tempn = 1, 0xFFFFFFFF
    src += dest
    macl = (macl + tempm) & M32
    ans = (0 if s32(macl) >= 0 else 1) + dest
    if S == 1:
        if ans == 1:
            if src == 0:
                macl, mach = 0x7FFFFFFF, mach | 1
            if src == 2:
                macl, mach = 0x80000000, mach | 1
    else:
        mach = (mach + tempn) & M32
        if templ > macl:
            mach = (mach + 1) & M32
    return mach, macl


def macl_manual(mach, macl, ln, lm, S):
    """pm.txt 10.53 MACL(), literally, with 'if (MACH&0x00008000); else Res2+=MACH|0xFFFF0000;' read as the
    sign extension it is ('if (MACH&0x8000) Res2+=MACH|0xFFFF0000; else Res2+=MACH&0x7FFF;')"""
    tempn, tempm = isa.s32(ln), isa.s32(lm)
    fnLmL = -1 if (tempn ^ tempm) < 0 else 0
    temp1, temp2 = abs(tempn) & M32, abs(tempm) & M32
    RnL, RnH, RmL, RmH = temp1 & 0xFFFF, (temp1 >> 16) & 0xFFFF, temp2 & 0xFFFF, (temp2 >> 16) & 0xFFFF
    temp0, temp1, temp2, temp3 = RmL * RnL, RmH * RnL, RmL * RnH, RmH * RnH
    Res2 = 0
    Res1 = (temp1 + temp2) & M32
    if Res1 < temp1:
        Res2 += 0x00010000
    temp1 = (Res1 << 16) & 0xFFFF0000
    Res0 = (temp0 + temp1) & M32
    if Res0 < temp0:
        Res2 += 1
    Res2 = (Res2 + ((Res1 >> 16) & 0xFFFF) + temp3) & M32
    if fnLmL < 0:
        Res2 = ~Res2 & M32
        if Res0 == 0:
            Res2 = (Res2 + 1) & M32
        else:
            Res0 = (~Res0 + 1) & M32
    if S == 1:
        Res0 = (macl + Res0) & M32
        if macl > Res0:
            Res2 = (Res2 + 1) & M32
        Res2 = (Res2 + ((mach | 0xFFFF0000) if mach & 0x8000 else (mach & 0x7FFF))) & M32
        if isa.s32(Res2) < 0 and Res2 < 0xFFFF8000:
            Res2, Res0 = 0xFFFF8000, 0
        if isa.s32(Res2) > 0 and Res2 > 0x00007FFF:
            Res2, Res0 = 0x00007FFF, 0xFFFFFFFF
        return (Res2 & 0xFFFF) | (mach & 0xFFFF0000), Res0
    Res0 = (macl + Res0) & M32
    if macl > Res0:
        Res2 += 1
    Res2 = (Res2 + mach) & M32
    return Res2, Res0


def fp_ok_single(v):
    """an FCMP operand isa.py's raw compare handles: zero, normal or infinity (no NaN, no denormal)"""
    e = (v >> 23) & 0xFF
    return (e != 0 or v & 0x7FFFFFFF == 0) and not (e == 0xFF and v & 0x7FFFFF)


def one_vector(form, mode, fidx, midx, k):
    """returns the vector line, or None when isa.py rejects the random case"""
    seed = mix64(((fidx << 40) | (midx << 32) | k) ^ 0x5eed5eed00000000)
    R = random.Random(seed)
    st0 = gen_state(seed)
    ov = {}
    pr, sz = isa.MODES[mode]
    ov['fpscr'] = (st0['fpscr'] & ~(3 << 19)) | (pr << 19) | (sz << 20)
    sr = st0['sr']
    if form.name in PRIV:
        sr |= 0x40000000
    if form.br in ('cond_t', 'cond_f'):
        sr = (sr & ~1) | int(form.flags['want_t'])
    ov['sr'] = sr
    # fields
    f = {}
    for ch, (sh, wd) in form.fields.items():
        f[ch] = R.getrandbits(wd)
    if form.name.startswith('MAC.'):
        while f['n'] == f['m']:
            f['m'] = R.getrandbits(4)
    pc = (R.choice((0x0C, 0x8C, 0xAC)) << 24) | 0x001000 | (R.getrandbits(11) << 1)
    regs = dict(st0)
    regs.update(ov)

    def setreg(key, v):
        regs[key] = v & M32
        ov[key] = v & M32

    fixed = set()
    for i, mem in enumerate(form.mems):
        if mem.mode == 'pc':
            continue
        sz_ = mem.size
        T = (R.choice((0x0C, 0x8C, 0xAC)) << 24) | 0x100000 | (i << 16) | (R.getrandbits(15) & ~(max(sz_, 4) - 1))
        if mem.mode in ('gbr0', 'gbr'):
            if mem.mode == 'gbr0':
                if 'r0' not in fixed:
                    setreg('r0', R.getrandbits(12))
                    fixed.add('r0')
                setreg('gbr', T - regs['r0'])
            else:
                setreg('gbr', T - f['d'] * sz_)
            continue
        b = f'r{f[mem.base[1]]}'
        if mem.mode == 'ind' or mem.mode == 'post':
            bv = T
        elif mem.mode == 'pre':
            bv = T + sz_
        elif mem.mode == 'disp':
            bv = T - f['d'] * sz_
        elif mem.mode == 'r0':
            if b == 'r0':
                setreg('r0', T >> 1)
                fixed.add('r0')
                continue
            if 'r0' not in fixed:
                setreg('r0', R.getrandbits(16))
                fixed.add('r0')
            bv = T - regs['r0']
        else:
            raise ValueError(mem.mode)
        if b in fixed:
            return None
        setreg(b, bv)
        fixed.add(b)
    if form.br in ('jmp', 'jsr', 'braf', 'bsrf'):
        setreg(f'r{f["n"]}', regs[f'r{f["n"]}'] & ~1)
    if form.br == 'rts':
        setreg('pr', regs['pr'] & ~1)
    if form.br == 'rte':
        setreg('spc', regs['spc'] & ~1)
    # FCMP: operands isa.py compares correctly (see the module docstring)
    if form.name.startswith('FCMP'):
        frb = (regs['fpscr'] >> 21) & 1
        bk = 'ab'[frb]
        r2 = Rng(seed ^ 0xFC)
        if 'DR' in form.name:
            for x in (2 * f['n'], 2 * f['m']):
                v = fpword(r2)
                while not fp_ok_single(v):
                    v = fpword(r2)
                setreg(f'f{bk}{x}', v)
                setreg(f'f{bk}{x + 1}', 0)
        else:
            for x in (f['n'], f['m']):
                v = regs[f'f{bk}{x}']
                while not fp_ok_single(v):
                    v = fpword(r2)
                setreg(f'f{bk}{x}', v)
    op = form.encode(f)
    # isa.py state
    st = RecState()
    st.mem_ok = [(0, 1 << 32, 'rw')]
    for key in STATE_KEYS:
        if key == 'sr':
            continue
        st.reg[ISA_OF_KEY[key]] = regs[key]
    st.reg['src'], st.reg['t'], st.reg['s'], st.reg['qm'] = regs['sr'], regs['sr'] & 1, (regs['sr'] >> 1) & 1, \
        (regs['sr'] >> 8) & 3
    st.fpscr_mode = regs['fpscr']
    st.reg['pte'] = 0
    code = {pc: op & 0xFF, pc + 1: op >> 8}
    slot = 0x0009 if form.delayed else None
    if slot is not None:
        code[pc + 2], code[pc + 3] = slot & 0xFF, slot >> 8
    # memory the instruction may read: hash bytes (and the code words, which a PC-relative load can reach)
    for mem in form.mems:
        a, _ = isa.ea(st, form, mem, f, pc)
        for j in range(-8, mem.size + 8):
            x = (a + j) & M32
            st.mem[x] = code.get(x, memb(seed, x))
    st.trace_r, st.trace_w = set(), set()
    pre = dict(st.reg)
    try:
        res = isa.execute(st, form, f, pc)
    except isa.Bad:
        return None
    # ---- expected registers
    exp = {}
    for nm in st.trace_w:
        if nm in ('src', 't', 's', 'qm', 'tlb', 'mem', 'pte'):
            continue
        exp[KEY_OF_ISA[nm]] = st.reg[nm]
    if st.trace_w & {'src', 't', 's', 'qm'}:
        src, t, s_, qm = st.reg['src'], st.reg['t'], st.reg['s'], st.reg['qm']
        exp['sr'] = None if None in (src, t, s_, qm) else ((src & ~0x303) | t | (s_ << 1) | (qm << 8)) & SR_MASK
    if 'fpscr' in exp:
        if form.name in ('FRCHG', 'FSCHG'):
            exp['fpscr'] = st.fpscr_mode
        elif exp['fpscr'] is not None:
            exp['fpscr'] &= FPSCR_MASK
    if form.name.startswith('MAC.'):
        an, am = pre[f'r{f["n"]}'], pre[f'r{f["m"]}']
        sz_ = form.mems[0].size
        vn = sum(st.mem[(an + j) & M32] << (8 * j) for j in range(sz_))
        vm = sum(st.mem[(am + j) & M32] << (8 * j) for j in range(sz_))
        fn = macl_manual if sz_ == 4 else macw_manual
        exp['mach'], exp['macl'] = fn(pre['mach'], pre['macl'], vn, vm, pre['s'])
    # SR written with another effective bank: R0-R7 and the other bank swap (isa.py has no banks)
    if exp.get('sr') is not None:
        ob = (regs['sr'] >> 30) & (regs['sr'] >> 29) & 1
        nb = (exp['sr'] >> 30) & (exp['sr'] >> 29) & 1
        if ob != nb:
            for j in range(8):
                rj = exp.get(f'r{j}', regs[f'r{j}'])
                bj = exp.get(f'b{j}', regs[f'b{j}'])
                exp[f'r{j}'], exp[f'b{j}'] = bj, rj
    # ---- stores (physical byte address -> value)
    wr = {}
    for a, size, v in st.log:
        for j in range(size):
            x = (a + j) & M32
            b = st.mem.get(x)
            wr[x & PHYS] = b
    # ---- control flow
    if form.delayed:
        nsteps, npc = 2, (res[0] if res else pc + 4) & M32
    else:
        nsteps, npc = 1, (res[0] if res else pc + 2) & M32
    toks = [f'V {fidx} {midx} {seed:x} {op:04x} {slot if slot is not None else 0:04x} {pc:x} {nsteps} {npc:x} '
            f'{checksum(st0):x}']
    toks += [f'o.{k}={v:x}' for k, v in ov.items()]
    toks += [f'e.{k}=' + ('?' if v is None else f'{v & M32:x}') for k, v in sorted(exp.items())]
    toks += [f'w.{a:x}=' + ('?' if v is None else f'{v:x}') for a, v in sorted(wr.items())]
    return ' '.join(toks)


def int_job(job):
    fidx, name, nper = job
    form = isa.BYNAME[name]
    out, bad = [], 0
    for midx, mode in enumerate(('M0', 'M1', 'M2')):
        if mode not in form.modes:
            continue
        n = max(1, nper // len(form.modes))
        k = got = 0
        while got < n and k < 20 * n:
            line = one_vector(form, mode, fidx, midx, k)
            k += 1
            if line is None:
                bad += 1
                continue
            out.append(line)
            got += 1
    return out, bad


# -------------------------------------------------------------------------------------------------------------------
# Exact FP reference (no host floating point anywhere)
class Fmt:
    def __init__(self, p, ebits):
        self.p, self.ebits, self.fbits = p, ebits, p - 1
        self.bias = (1 << (ebits - 1)) - 1
        self.emin, self.emax = 1 - self.bias, self.bias
        self.bits = 1 + ebits + self.fbits
        self.sbit = 1 << (self.bits - 1)
        self.emask = (1 << ebits) - 1
        self.inf = self.emask << self.fbits
        self.maxf = self.inf - 1
        self.qnan = 0x7FBFFFFF if p == 24 else 0x7FF7FFFFFFFFFFFF


SGL, DBL = Fmt(24, 8), Fmt(53, 11)
FI, FU, FO, FZ, FV = 1, 2, 4, 8, 16


def unpk(x, F):
    """-> (class, sign, mant, exp) with value = mant * 2^exp; DN = 1: exponent 0 is a zero"""
    s = x >> (F.bits - 1)
    e = (x >> F.fbits) & F.emask
    fr = x & ((1 << F.fbits) - 1)
    if e == F.emask:
        if fr == 0:
            return 'inf', s, 0, 0
        return ('snan' if fr >> (F.fbits - 1) else 'qnan'), s, 0, 0
    if e == 0:
        return 'zero', s, 0, 0
    return 'num', s, fr | (1 << F.fbits), e - F.bias - F.fbits


def rnd(s, mant, exp, sticky, F, rz):
    """round (mant + sticky * tiny) * 2^exp (mant > 0) to F; -> (bits, cause).  Gradual underflow first, then a
    denormal result is flushed (U | I); overflow per IEEE with the rounded-unbounded result."""
    nb = mant.bit_length()
    e = nb - 1 + exp
    qe = max(e, F.emin) - (F.p - 1)
    sh = qe - exp
    if sh > 0:
        q, rem, half = mant >> sh, mant & ((1 << sh) - 1), 1 << (sh - 1)
        inexact = rem != 0 or sticky
        if not rz and (rem > half or (rem == half and (sticky or (q & 1)))):
            q += 1
    else:
        assert not sticky
        q, inexact = mant << (-sh), False
    sgn = s * F.sbit
    if q == 0:
        return sgn, FU | FI
    if q >> F.p:
        q >>= 1
        qe += 1
    if qe + q.bit_length() - 1 > F.emax:
        return sgn | (F.maxf if rz else F.inf), FO | FI
    if q < (1 << (F.p - 1)):
        return sgn, FU | FI
    return sgn | ((qe + F.fbits + F.bias) << F.fbits) | (q - (1 << F.fbits)), FI if inexact else 0


def nanres(ua, ub=('num',), uc=('num',), F=SGL):
    if 'snan' in (ua[0], ub[0], uc[0]):
        return F.qnan, FV
    return F.qnan, 0


def r_add(a, b, F, rz):
    ua, ub = unpk(a, F), unpk(b, F)
    if ua[0] in ('snan', 'qnan') or ub[0] in ('snan', 'qnan'):
        return nanres(ua, ub, F=F)
    if ua[0] == 'inf' and ub[0] == 'inf':
        return (F.qnan, FV) if ua[1] != ub[1] else (a, 0)
    if ua[0] == 'inf':
        return a, 0
    if ub[0] == 'inf':
        return b, 0
    if ua[0] == 'zero' and ub[0] == 'zero':
        return (ua[1] & ub[1]) * F.sbit, 0
    if ua[0] == 'zero':
        return b, 0
    if ub[0] == 'zero':
        return a, 0
    e0 = min(ua[3], ub[3])
    S = (-1 if ua[1] else 1) * (ua[2] << (ua[3] - e0)) + (-1 if ub[1] else 1) * (ub[2] << (ub[3] - e0))
    if S == 0:
        return 0, 0
    return rnd(1 if S < 0 else 0, abs(S), e0, False, F, rz)


def r_mul(a, b, F, rz):
    ua, ub = unpk(a, F), unpk(b, F)
    if ua[0] in ('snan', 'qnan') or ub[0] in ('snan', 'qnan'):
        return nanres(ua, ub, F=F)
    s = ua[1] ^ ub[1]
    if (ua[0] == 'inf' and ub[0] == 'zero') or (ua[0] == 'zero' and ub[0] == 'inf'):
        return F.qnan, FV
    if 'inf' in (ua[0], ub[0]):
        return s * F.sbit | F.inf, 0
    if 'zero' in (ua[0], ub[0]):
        return s * F.sbit, 0
    return rnd(s, ua[2] * ub[2], ua[3] + ub[3], False, F, rz)


def r_div(a, b, F, rz):
    ua, ub = unpk(a, F), unpk(b, F)
    if ua[0] in ('snan', 'qnan') or ub[0] in ('snan', 'qnan'):
        return nanres(ua, ub, F=F)
    s = ua[1] ^ ub[1]
    if (ua[0] == 'inf' and ub[0] == 'inf') or (ua[0] == 'zero' and ub[0] == 'zero'):
        return F.qnan, FV
    if ua[0] == 'inf':
        return s * F.sbit | F.inf, 0
    if ub[0] == 'inf' or ua[0] == 'zero':
        return s * F.sbit, 0
    if ub[0] == 'zero':
        return s * F.sbit | F.inf, FZ
    K = 2 * F.p + 4
    q, r = divmod(ua[2] << K, ub[2])
    return rnd(s, q, ua[3] - ub[3] - K, r != 0, F, rz)


def r_sqrt(a, F, rz):
    ua = unpk(a, F)
    if ua[0] in ('snan', 'qnan'):
        return nanres(ua, F=F)
    if ua[0] == 'zero':
        return ua[1] * F.sbit, 0
    if ua[1]:
        return F.qnan, FV
    if ua[0] == 'inf':
        return a, 0
    m, e = ua[2], ua[3]
    if e & 1:
        m, e = m << 1, e - 1
    K = F.p + 2
    X = m << (2 * K)
    root = isqrt(X)
    return rnd(0, root, (e - 2 * K) // 2, root * root != X, F, rz)


def r_mac(a, b, c, rz):
    F = SGL
    ua, ub, uc = unpk(a, F), unpk(b, F), unpk(c, F)
    if 'snan' in (ua[0], ub[0], uc[0]):
        return F.qnan, FV
    if ua[0] == 'qnan' or ub[0] == 'qnan':
        return F.qnan, 0
    if (ua[0] == 'inf' and ub[0] == 'zero') or (ua[0] == 'zero' and ub[0] == 'inf'):
        return F.qnan, FV
    if uc[0] == 'qnan':
        return F.qnan, 0
    sp = ua[1] ^ ub[1]
    if 'inf' in (ua[0], ub[0]):
        if uc[0] == 'inf' and uc[1] != sp:
            return F.qnan, FV
        return sp * F.sbit | F.inf, 0
    if uc[0] == 'inf':
        return c, 0
    if 'zero' in (ua[0], ub[0]):
        if uc[0] == 'zero':
            return (sp & uc[1]) * F.sbit, 0
        return c, 0
    P, ep = ua[2] * ub[2], ua[3] + ub[3]
    if uc[0] == 'zero':
        return rnd(sp, P, ep, False, F, rz)
    e0 = min(ep, uc[3])
    S = (-1 if sp else 1) * (P << (ep - e0)) + (-1 if uc[1] else 1) * (uc[2] << (uc[3] - e0))
    if S == 0:
        return 0, 0
    return rnd(1 if S < 0 else 0, abs(S), e0, False, F, rz)


def r_float(i, F, rz):
    v = isa.s32(i)
    if v == 0:
        return 0, 0
    return rnd(1 if v < 0 else 0, abs(v), 0, False, F, rz)


def r_ftrc(a, F):
    ua = unpk(a, F)
    if ua[0] in ('snan', 'qnan'):
        return 0x80000000, FV
    hi = a >> (F.bits - 32)
    if F is SGL:
        pbig, nbig = hi > 0x4EFFFFFF, hi > 0xCF000000
    else:
        pbig, nbig = hi > 0x41DFFFFF, hi > 0xC1E00000
    if not ua[1] and pbig:
        return 0x7FFFFFFF, FV
    if ua[1] and nbig:
        return 0x80000000, FV
    if ua[0] == 'zero':
        return 0, 0
    mag = ua[2] >> -ua[3] if ua[3] < 0 else ua[2] << ua[3]
    v = -mag if ua[1] else mag
    if v < -0x80000000:
        v = -0x80000000                  # DR high-word window: saturate, no V (sh4_iss.h fp::ftrc_d)
    return v & M32, 0


def r_cmp(a, b, F, gt):
    """FCMP/EQ (gt False) / FCMP/GT: T = a == b / a > b (a = FRn, b = FRm) -> (T, cause)"""
    ua, ub = unpk(a, F), unpk(b, F)
    nan = ua[0] in ('snan', 'qnan') or ub[0] in ('snan', 'qnan')
    if nan:
        if gt or 'snan' in (ua[0], ub[0]):
            return 0, FV
        return 0, 0

    def val(u):
        if u[0] == 'zero':
            return Fraction(0)
        if u[0] == 'inf':
            return Fraction(-(1 << 2000) if u[1] else (1 << 2000))
        return Fraction((-1 if u[1] else 1) * u[2]) * (Fraction(2) ** u[3])
    va, vb = val(ua), val(ub)
    return int(va > vb if gt else va == vb), 0


def r_cnvsd(a):
    ua = unpk(a, SGL)
    if ua[0] in ('snan', 'qnan'):
        return nanres(ua, F=DBL)
    if ua[0] == 'zero':
        return ua[1] << 63, 0
    if ua[0] == 'inf':
        return (ua[1] << 63) | DBL.inf, 0
    return rnd(ua[1], ua[2], ua[3], False, DBL, False)


def r_cnvds(a, rz):
    ua = unpk(a, DBL)
    if ua[0] in ('snan', 'qnan'):
        return nanres(ua, F=SGL)
    if ua[0] == 'zero':
        return ua[1] << 31, 0
    if ua[0] == 'inf':
        return (ua[1] << 31) | SGL.inf, 0
    return rnd(ua[1], ua[2], ua[3], False, SGL, rz)


# ---- operand generators
def specials(F):
    if F is SGL:
        return [0, 0x80000000, 0x7F800000, 0xFF800000, 0x7FBFFFFF, 0x7F800001, 0xFFBFFFFF, 0x7FC00000, 0xFFFFFFFF,
                0x00000001, 0x807FFFFF, 0x00800000, 0x80800000, 0x7F7FFFFF, 0xFF7FFFFF, 0x3F800000, 0xBF800000,
                0x3F800001, 0x3F7FFFFF, 0x4F000000, 0xCF000000, 0x4EFFFFFF, 0xCF000001, 0x00800001, 0x33800000]
    return [0, 1 << 63, DBL.inf, DBL.inf | (1 << 63), 0x7FF7FFFFFFFFFFFF, 0x7FF0000000000001, 0x7FF8000000000000,
            0xFFFFFFFFFFFFFFFF, 1, 0x800FFFFFFFFFFFFF, 0x0010000000000000, 0x7FEFFFFFFFFFFFFF, 0x3FF0000000000000,
            0xBFF0000000000000, 0x3FF0000000000001, 0x41E0000000000000, 0xC1E0000000000000, 0x41DFFFFFFFFFFFFF,
            0xC1E0000000000001, 0xC1E00000001FFFFF, 0xC1E0000000200000, 0x47EFFFFFE0000000, 0x47EFFFFFF0000000,
            0x3810000000000000, 0x380FFFFFF0000000, 0x36A0000000000000]


def num(R, F, s, e, frac):
    return (s << (F.bits - 1)) | ((e & F.emask) << F.fbits) | (frac & ((1 << F.fbits) - 1))


def gen_op(R, F, erange=None):
    c = R.randrange(12)
    s = R.getrandbits(1)
    if c == 0:
        return R.getrandbits(F.bits)
    if c == 1:
        return R.choice(specials(F))
    if c == 2:                                          # short mantissa: exact / tie products and sums
        k = R.randint(1, F.p)
        m = R.getrandbits(k - 1) | (1 << (k - 1))
        e = R.randint(1, F.emask - 1) if erange is None else R.randint(*erange)
        return num(R, F, s, e, (m << (F.p - k)) - (1 << F.fbits))
    if c == 3:                                          # power of two
        e = R.randint(1, F.emask - 1) if erange is None else R.randint(*erange)
        return num(R, F, s, e, 0)
    if c == 4:                                          # denormal
        return num(R, F, s, 0, R.getrandbits(F.fbits) | 1)
    if c == 5:                                          # near max
        return num(R, F, s, F.emask - 1 - R.randrange(4), R.getrandbits(F.fbits))
    if c == 6:                                          # near min normal
        return num(R, F, s, 1 + R.randrange(4), R.getrandbits(F.fbits))
    if c == 7:                                          # all-ones / near-all-ones mantissa
        return num(R, F, s, R.randint(1, F.emask - 1) if erange is None else R.randint(*erange),
                   ((1 << F.fbits) - 1) ^ R.getrandbits(3))
    e = R.randint(1, F.emask - 1) if erange is None else R.randint(*erange)
    return num(R, F, s, e, R.getrandbits(F.fbits))


def gen_pair(R, F, kind):
    """two operands with a relation that reaches the interesting rounding / range cases of `kind`"""
    a = gen_op(R, F)
    c = R.randrange(8)
    ea = (a >> F.fbits) & F.emask
    if c < 3 or ea in (0, F.emask):
        return a, gen_op(R, F)
    s = R.getrandbits(1)
    if kind == 'add':                                  # close exponents: cancellation, sticky / round-bit edges
        d = R.randint(-F.p - 3, F.p + 3)
        eb = min(max(ea + d, 1), F.emask - 1)
        fr = R.getrandbits(F.fbits) if R.randrange(2) else (a & ((1 << F.fbits) - 1)) ^ R.getrandbits(R.randint(0, 4))
        return a, num(R, F, s, eb, fr)
    if kind in ('mul', 'div'):                         # exponent sum / difference at the overflow / underflow edges
        tgt = R.choice((F.emax, F.emin, F.emin - F.p, F.emin - 1, F.emax + 1)) + R.randint(-2, 2)
        eu = (tgt - (ea - F.bias)) if kind == 'mul' else ((ea - F.bias) - tgt)
        eb = min(max(eu + F.bias, 1), F.emask - 1)
        k = R.randint(1, F.p)
        fr = ((R.getrandbits(k - 1) | (1 << (k - 1))) << (F.p - k)) - (1 << F.fbits) if R.randrange(2) \
            else R.getrandbits(F.fbits)
        return a, num(R, F, s, eb, fr)
    return a, gen_op(R, F)


def fp_job(job):
    opname, nper, seed = job
    R = random.Random(seed)
    out = []
    for rz in (0, 1):
        for _ in range(nper):
            a = b = c = 0
            if opname in ('add_s', 'sub_s', 'mul_s', 'div_s', 'cmpeq_s', 'cmpgt_s'):
                a, b = gen_pair(R, SGL, opname[:3] if opname[:3] in ('add', 'sub', 'mul', 'div') else 'x')
                if opname == 'sub_s' and R.randrange(4) == 0:
                    b = a ^ R.getrandbits(3)
                if opname.startswith('cmp') and R.randrange(4) == 0:
                    b = R.choice((a, a ^ 0x80000000, a ^ 1))
            elif opname in ('add_d', 'sub_d', 'mul_d', 'div_d', 'cmpeq_d', 'cmpgt_d'):
                a, b = gen_pair(R, DBL, opname[:3] if opname[:3] in ('add', 'sub', 'mul', 'div') else 'x')
                if opname.startswith('cmp') and R.randrange(4) == 0:
                    b = R.choice((a, a ^ (1 << 63), a ^ 1))
            elif opname == 'sqrt_s':
                a = gen_op(R, SGL)
                if R.randrange(3) == 0:                  # exact squares and their neighbours
                    m = R.getrandbits(12) | (1 << 11)
                    r0, _ = r_mul(num(R, SGL, 0, 127, (m << 12) - (1 << 23)), num(R, SGL, 0, 127, (m << 12) - (1 << 23)), SGL, 0)
                    a = (r0 + R.randint(-1, 1)) & M32
            elif opname == 'sqrt_d':
                a = gen_op(R, DBL)
            elif opname == 'mac_s':
                a, b = gen_pair(R, SGL, 'mul')
                c = gen_op(R, SGL)
                if R.randrange(3) == 0:                  # cancellation against the rounded product
                    p, _ = r_mul(a, b, SGL, R.randrange(2))
                    c = (p ^ 0x80000000) ^ R.getrandbits(R.randint(0, 3))
                elif R.randrange(2) == 0:                # c +- (half an ulp of c) -+ tiny: exact result a hair off
                    # a = (1 + i 2^-23) 2^ka, b = (1 - i 2^-23) 2^kb: a b = (1 - i^2 2^-46) 2^(ka + kb) with ka + kb =
                    # the exponent of half an ulp of c (or of a whole ulp: the RZ / representable edge)
                    ec = R.randint(60, 225)                  # a tie; one rounding must see the hair, two would not
                    c = num(R, SGL, R.getrandbits(1), ec, R.getrandbits(23))
                    i = R.randint(1, 1 << R.randint(0, 11))
                    ka = R.randint(-20, 20)
                    a = num(R, SGL, R.getrandbits(1), 127 + ka, i)
                    b = num(R, SGL, R.getrandbits(1), ec - 25 - ka + R.randint(0, 1), (1 << 23) - 2 * i)
                elif R.randrange(3) == 0:                # addend a few binades from the product
                    p, _ = r_mul(a, b, SGL, 0)
                    ep = (p >> 23) & 0xFF
                    if 0 < ep < 255:
                        c = num(R, SGL, R.getrandbits(1), min(max(ep + R.randint(-26, 26), 1), 254), R.getrandbits(23))
            elif opname in ('float_s', 'float_d'):
                cc = R.randrange(4)
                a = R.getrandbits(32) if cc == 0 else R.choice(EDGE) if cc == 1 else \
                    ((1 << R.randrange(32)) + R.randint(-3, 3)) & M32 if cc == 2 else (-(R.getrandbits(R.randint(1, 31)))) & M32
            elif opname == 'ftrc_s':
                a = gen_op(R, SGL, (120, 160))
            elif opname == 'ftrc_d':
                a = gen_op(R, DBL, (1000, 1060))
                if R.randrange(4) == 0:
                    a = (R.choice((0x41E0, 0xC1E0, 0x41DF, 0xC1DF)) << 48) | R.getrandbits(48)
            elif opname == 'cnvsd':
                a = gen_op(R, SGL)
            elif opname == 'cnvds':
                a = gen_op(R, DBL, (1023 - 160, 1023 + 130))
                if R.randrange(3) == 0:                  # 24-bit round-bit / tie patterns in the low 29 bits
                    a = (a & ~((1 << 29) - 1)) | R.choice((1 << 28, (1 << 28) | 1, (1 << 28) - 1, 0, (1 << 29) - 1))
            r = cause = 0
            if opname == 'add_s':
                r, cause = r_add(a, b, SGL, rz)
            elif opname == 'sub_s':
                r, cause = r_add(a, b ^ 0x80000000, SGL, rz)
            elif opname == 'mul_s':
                r, cause = r_mul(a, b, SGL, rz)
            elif opname == 'div_s':
                r, cause = r_div(a, b, SGL, rz)
            elif opname == 'sqrt_s':
                r, cause = r_sqrt(a, SGL, rz)
            elif opname == 'mac_s':
                r, cause = r_mac(a, b, c, rz)
            elif opname == 'float_s':
                r, cause = r_float(a, SGL, rz)
            elif opname == 'ftrc_s':
                r, cause = r_ftrc(a, SGL)
            elif opname == 'cmpeq_s':
                r, cause = r_cmp(a, b, SGL, False)
            elif opname == 'cmpgt_s':
                r, cause = r_cmp(a, b, SGL, True)
            elif opname == 'add_d':
                r, cause = r_add(a, b, DBL, rz)
            elif opname == 'sub_d':
                r, cause = r_add(a, b ^ (1 << 63), DBL, rz)
            elif opname == 'mul_d':
                r, cause = r_mul(a, b, DBL, rz)
            elif opname == 'div_d':
                r, cause = r_div(a, b, DBL, rz)
            elif opname == 'sqrt_d':
                r, cause = r_sqrt(a, DBL, rz)
            elif opname == 'float_d':
                r, cause = r_float(a, DBL, rz)
            elif opname == 'ftrc_d':
                r, cause = r_ftrc(a, DBL)
            elif opname == 'cmpeq_d':
                r, cause = r_cmp(a, b, DBL, False)
            elif opname == 'cmpgt_d':
                r, cause = r_cmp(a, b, DBL, True)
            elif opname == 'cnvsd':
                r, cause = r_cnvsd(a)
            elif opname == 'cnvds':
                r, cause = r_cnvds(a, rz)
            out.append(f'{opname} {rz} {a:x} {b:x} {c:x} {r:x} {cause:x}')
    return out


FP_OPS = ['add_s', 'sub_s', 'mul_s', 'div_s', 'sqrt_s', 'mac_s', 'float_s', 'ftrc_s', 'cmpeq_s', 'cmpgt_s',
          'add_d', 'sub_d', 'mul_d', 'div_d', 'sqrt_d', 'float_d', 'ftrc_d', 'cmpeq_d', 'cmpgt_d', 'cnvsd', 'cnvds']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out', nargs='?', default=os.path.join(HERE, '..', 'build', 'v1_iss'))
    ap.add_argument('--int', type=int, default=3000, help='isa.py vectors per form (split over its modes)')
    ap.add_argument('--fp', type=int, default=40000, help='FP vectors per op and rounding mode (split in jobs)')
    ap.add_argument('--jobs', type=int, default=os.cpu_count())
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    names = iss_names()
    # decode
    tabs = decode_table(names)
    with open(os.path.join(args.out, 'decode.txt'), 'w') as fo:
        fo.write(f'N {len(names)}\n')
        for i, n in enumerate(names):
            fo.write(f'F {i} {n}\n')
        for t in tabs:
            for k in range(0, 65536, 16):
                fo.write(' '.join(str(x) for x in t[k:k + 16]) + '\n')
    # isa.py vectors
    fidx = {n: i for i, n in enumerate(names)}
    jobs = [(fidx[base_name(f.name)] * 2 + (1 if '(not taken)' in f.name else 0), f.name, args.int)
            for f in isa.FORMS]
    nvec = nbad = 0
    with multiprocessing.Pool(args.jobs) as pool, open(os.path.join(args.out, 'int.txt'), 'w') as fo:
        for lines, bad in pool.imap(int_job, jobs):
            for ln in lines:
                fo.write(ln + '\n')
            nvec += len(lines)
            nbad += bad
    # FP vectors: each op split over a few jobs
    split = 8
    per = max(1, args.fp // split)
    fjobs = [(op, per, 1000 * i + k) for i, op in enumerate(FP_OPS) for k in range(split)]
    nfp = 0
    with multiprocessing.Pool(args.jobs) as pool, open(os.path.join(args.out, 'fp.txt'), 'w') as fo:
        for lines in pool.imap(fp_job, fjobs):
            for ln in lines:
                fo.write(ln + '\n')
            nfp += len(lines)
    print(f'v1_iss_vectors: decode 3 x 65536, {nvec} isa.py vectors ({nbad} random cases isa.py rejected and '
          f'redrawn), {nfp} FP vectors -> {args.out}', file=sys.stderr)


if __name__ == '__main__':
    main()
