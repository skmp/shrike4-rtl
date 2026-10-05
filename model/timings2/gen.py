#!/usr/bin/env python3
"""Generate the pairwise timing cases for the SH-4 pipeline sweep (see README.md).

For every FPU mode and every ordered pair (A, B) of instruction forms valid in it, every dependency variant:
  ind              no register / memory overlap except the implicit resources both must use (T, FPUL, FPSCR, ...)
  ov:<a>><b>[k]    operand a of A and operand b of B share physical registers (one element, or the k-th aligned
                   sub-vector); at least one side writes -> RAW / WAW / WAR by the access types
  mem:same / mem:line   both access memory: the same address / another word of the same 32-byte line
each case is one loop body, timed at every distance n (dependent `add #1,r14` pads between A and B):
    L: A ; pad x n ; B ; pad x DRAIN ; restores ; dt r13 ; bf/s L ; <T fix>
Before it is emitted a case runs on the functional model (isa.py) for two iterations: control flow must be linear,
every memory access inside the case scratch, and every value reaching an address, a branch target, SR / FPSCR / VBR /
GBR or a conditional branch's T exactly what it must be; registers that such values depend on and that the pair
changes are restored at the end of the body.  If A's result feeds such a slot of B, A's inputs are searched until
A produces an acceptable value.  Cases that cannot be made safe are not emitted: tests/cases_skipped.jsonl.

usage: gen.py [--probe probe.txt] [--out tests] [--chunk 1500] [--modes M0,M1,M2] [--pairs "A|B|M0;..."] [--align 0]
"""
import argparse
import itertools
import multiprocessing
import json
import os
import struct
import sys

import isa
from isa import Bad, State, execute, op_elems, field_kinds, fr_name, BYNAME, FORMS, MODES, M32

HERE = os.path.dirname(os.path.abspath(__file__))

N_LIST = list(range(0, 17)) + [40]    # distances timed per case; 40 is the far reference
DRAIN = 40                            # pads after B
FPSCR_BASE = 0x00040000               # DN = 1, RN, no enables
MAX_BODY = 126                        # halfwords from L to bf/s (8-bit branch displacement)

# per-case scratch layout (offsets from S; r12 = S during the test)
RESTORE_OFF, RESTORE_WORDS = 0, 8      # restore values, read-only for the tested instructions
HARD_OFF = 32                          # words the restores can rewrite (@(disp,r12) reaches S+60)
GEN_LO = 64                            # general data S+64 .. S+255 (GBR = S+64)
CACHEOP_LO = 256                       # lines for OCBI / OCBP / OCBWB / MOVCA / TAS / PREF
SCRATCH_SIZE = 512
POOL_OFF, POOL_SIZE = 384, 64          # literal pool at CODE + 384, word 0 = jit_exit
P_DEF_OFF = 160                        # every other memory word holds the pointer S + 160


class Layout:
    def __init__(self, probe):
        self.S = probe['scratch']
        self.CODE = probe['code']
        self.EXIT = probe['exit']
        self.SR = probe['sr'] & ~0x303
        self.VBR = probe['vbr']
        self.POOL = self.CODE + POOL_OFF
        self.P_DEF = self.S + P_DEF_OFF
        self.GBR = self.S + HARD_OFF


def read_probe(path):
    """layout values: from probe.txt (written by the runner), or from the runner ELF's symbols when `path` is an
    ELF (VBR = KOS's _irq_vma_table; SR while timing = MD | IMASK 15, as the runner reports it)"""
    if path.endswith('.elf'):
        import subprocess
        tool = 'sh-elf-nm' if __import__('shutil').which('sh-elf-nm') else '/opt/toolchains/dc/sh-elf/bin/sh-elf-nm'
        nm = subprocess.run([tool, path], capture_output=True, text=True, check=True).stdout
        sym = {l.split()[2]: int(l.split()[0], 16) for l in nm.splitlines() if len(l.split()) == 3}
        return {'scratch': sym['_scratch'], 'code': sym['_jit_code'], 'exit': sym['_jit_exit'],
                'vbr': sym['_irq_vma_table'], 'sr': 0x400000F0}
    p = {}
    for line in open(path):
        if '=' in line:
            k, v = line.strip().split('=', 1)
            p[k] = int(v, 16)
    return p


# ---------------------------------------------------------------------------------------------------------------
FIELD_CANDS = {
    'gpr': [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0],
    'fr': [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0],
    'dr': [1, 2, 3, 4, 5, 6, 7, 0],
    'xd': [1, 2, 3, 4, 5, 6, 7, 0],
    'fv': [1, 2, 3, 0],
    'bank': [1, 2, 3, 4, 5, 6, 7, 0],
}
FIELD_OPS = ('Rn', 'Rm', 'FRn', 'FRm', 'DRn', 'DRm', 'XDn', 'XDm', 'FVn', 'FVm', 'FIPRn', 'BANKn', 'BANKm')


def is_field_op(op):
    return op.name in FIELD_OPS


def op_class(elems):
    e = elems[0]
    if e[0] == 'r' and e[1:].isdigit():
        return 'gpr'
    if e[:2] in ('fa', 'fb'):
        return 'fp' + e[1]
    if e.startswith('bk'):
        return 'bank'
    return e


class Pair:
    """A then B in a base FPU mode.  FRCHG as A flips the bank B sees; FSCHG as A flips SZ for B."""

    def __init__(self, a, b, mode, align=0):
        self.a, self.b, self.mode, self.align = a, b, mode, align
        self.frb_a = 0
        self.frb_b = 1 if a.name == 'FRCHG' else 0


def dedup_ops(form):
    """one operand per name (an address register that is also a data operand is one register)"""
    out = {}
    for op in form.ops:
        if op.name in out:
            acc = set(out[op.name].acc) | set(op.acc)
            out[op.name] = isa.Operand(op.name, 'rw' if acc >= {'r', 'w'} else op.acc)
        else:
            out[op.name] = isa.Operand(op.name, op.acc)
    return list(out.values())


def overlap_spec(ea_, eb_, k):
    """the element set that must be shared for sub-vector position k"""
    big, small = (ea_, eb_) if len(ea_) >= len(eb_) else (eb_, ea_)
    w = len(small)
    return set(big[k * w:(k + 1) * w])


def allocate(pair, want):
    """field values for A and B so that exactly `want` = (a_op, b_op, k) overlaps (None: nothing avoidable)"""
    A, B = pair.a, pair.b
    opsA, opsB = dedup_ops(A), dedup_ops(B)
    kA, kB = field_kinds(A), field_kinds(B)
    slots = [('A', c) for c in 'nm' if kA.get(c) in FIELD_CANDS] + [('B', c) for c in 'nm' if kB.get(c) in FIELD_CANDS]

    def els(form, op, f, frb):
        try:
            return op_elems(form, op, f, frb)
        except KeyError:
            return None

    def self_ok(form, ops, f, frb):
        seen = {}
        for op in ops:
            es = els(form, op, f, frb)
            if es is None:
                continue
            for e in es:
                if e in seen and seen[e] != op.name and {seen[e], op.name} != {'FVn', 'FIPRn'}:
                    return False
                seen[e] = op.name
        return True

    def cross_ok(fa, fb, final):
        hit = False
        wanted_elems = set()
        if want is not None and final:
            for oa in opsA:
                for ob in opsB:
                    if oa.name == want[0] and ob.name == want[1]:
                        ea_, eb_ = els(A, oa, fa, pair.frb_a), els(B, ob, fb, pair.frb_b)
                        if ea_ is not None and eb_ is not None:
                            wanted_elems = overlap_spec(ea_, eb_, want[2])
        for oa in opsA:
            ea_ = els(A, oa, fa, pair.frb_a)
            if ea_ is None:
                continue
            for ob in opsB:
                eb_ = els(B, ob, fb, pair.frb_b)
                if eb_ is None:
                    continue
                inter = set(ea_) & set(eb_)
                is_want = want is not None and oa.name == want[0] and ob.name == want[1]
                if is_want:
                    if final:
                        if inter != overlap_spec(ea_, eb_, want[2]):
                            return False
                        hit = True
                    continue
                if not inter:
                    continue
                if not is_field_op(oa) and not is_field_op(ob):
                    continue            # unavoidable (both implicit)
                if final and inter <= wanted_elems:
                    continue            # the wanted register, seen through another operand (FIPR's FVn / FR4n+3)
                if not final and want is not None:
                    continue            # decided once every field is assigned
                return False            # any other sharing, even read-read, is avoided
        return hit or want is None or not final

    def rec(i, fa, fb):
        if i == len(slots):
            return (dict(fa), dict(fb)) if cross_ok(fa, fb, True) else None
        side, c = slots[i]
        form, ks, f, ops, frb = (A, kA, fa, opsA, pair.frb_a) if side == 'A' else (B, kB, fb, opsB, pair.frb_b)
        for v in FIELD_CANDS[ks[c]]:
            f[c] = v
            if not self_ok(form, ops, f, frb):
                continue
            if i == len(slots) - 1 or all(ch in fa for ch in [s[1] for s in slots if s[0] == 'A']):
                # prune once A is complete and some of B assigned: partial cross check is only safe at the end
                pass
            r = rec(i + 1, fa, fb)
            if r:
                return r
        del f[c]
        return None

    return rec(0, {}, {})


def variants(pair):
    A, B = pair.a, pair.b
    out = [('ind', None)]
    opsA, opsB = dedup_ops(A), dedup_ops(B)
    probe_f = {'n': 1, 'm': 2}
    for oa in opsA:
        for ob in opsB:
            if 'w' not in oa.acc and 'w' not in ob.acc:
                continue
            if not is_field_op(oa) and not is_field_op(ob):
                continue
            try:
                ea_ = op_elems(A, oa, probe_f, pair.frb_a)
                eb_ = op_elems(B, ob, probe_f, pair.frb_b)
            except KeyError:
                continue
            if op_class(ea_) != op_class(eb_):
                continue
            npos = max(len(ea_), len(eb_)) // min(len(ea_), len(eb_))
            for k in range(npos):
                out.append((f'ov:{oa.name}>{ob.name}' + (f'[{k}]' if npos > 1 else ''), (oa.name, ob.name, k)))
    for ia, ma in enumerate(A.mems):
        for ib, mb in enumerate(B.mems):
            if ma.mode == 'pc' or mb.mode == 'pc':
                continue
            out.append((f'mem:same{ia}{ib}' if len(A.mems) + len(B.mems) > 2 else 'mem:same', ('mem', ia, ib, 'same')))
            out.append((f'mem:line{ia}{ib}' if len(A.mems) + len(B.mems) > 2 else 'mem:line', ('mem', ia, ib, 'line')))
    return out


# ---------------------------------------------------------------------------------------------------------------
def slot_pos(n):
    return {'A': 0, 'B': 1 + n}


def mem_target(lay, side, i, mem, hardmem):
    S = lay.S
    if mem.kind in ('I', 'B', 'W', 'A', 'T', 'P'):
        return S + CACHEOP_LO + (0 if side == 'A' else 64) + 32 * i
    if hardmem:
        return S + HARD_OFF + (8 if side == 'A' else 16) + 4 * i
    return S + GEN_LO + (64 if side == 'A' else 128) + 16 * i


def initial_state(lay, mode, T_init):
    st = State()
    S = lay.S
    st.mem_ok = [(S + RESTORE_OFF, S + RESTORE_OFF + 4 * RESTORE_WORDS, 'ro'), (S + HARD_OFF, S + SCRATCH_SIZE, 'rw'),
                 (lay.POOL + 4, lay.POOL + POOL_SIZE, 'rw')]
    for k in range(11):
        st.reg[f'r{k}'] = 0x1000 + 0x10 * k
    st.reg['r11'] = 0
    st.reg['r12'] = S
    for k in range(16):
        st.reg[fr_name(0, k)] = 0x3F800000
        st.reg[fr_name(1, k)] = 0x3F800000 if k % 5 == 0 else 0
    st.reg['fpul'] = 0x3F800000
    pr, sz = MODES[mode]
    st.fpscr_mode = FPSCR_BASE | (pr << 19) | (sz << 20)
    st.reg['fpscr'] = st.fpscr_mode
    st.reg['src'] = lay.SR
    st.reg['t'] = T_init
    st.reg['s'] = 0
    st.reg['qm'] = 0
    st.reg['gbr'] = lay.GBR
    st.reg['ssr'] = lay.SR
    st.reg['spc'] = 0
    st.reg['dbr'] = 0
    for k in range(8):
        st.reg[f'bk{k}'] = 0x2000 + 0x10 * k
    st.reg['mach'] = 0
    st.reg['macl'] = 0
    st.reg['pr'] = 0x3000
    st.reg['vbr'] = lay.VBR
    st.reg['sgr'] = None
    st.reg['pte'] = 0
    for off in range(HARD_OFF, SCRATCH_SIZE, 4):
        for k in range(4):
            st.mem[S + off + k] = (lay.P_DEF >> (8 * k)) & 0xFF
    for k in range(RESTORE_OFF, RESTORE_OFF + 4 * RESTORE_WORDS):
        st.mem[S + k] = 0
    for off in range(4, POOL_SIZE, 4):
        for k in range(4):
            st.mem[lay.POOL + off + k] = (lay.P_DEF >> (8 * k)) & 0xFF
    return st


def setup_case(lay, pair, fa0, fb0, var, n, T_init):
    """initial state + complete field values for distance n"""
    A, B = pair.a, pair.b
    L = lay.CODE + pair.align
    st = initial_state(lay, pair.mode, T_init)
    st.reg['ssr'] = lay.SR | T_init
    fa, fb = dict(fa0), dict(fb0)
    for f, form in ((fa, A), (fb, B)):
        if 'i' in form.fields:
            f['i'] = 1
        if form.br in ('cond_t', 'cond_f'):
            f['d'] = 0x00 if form.delayed else 0xFF    # BT/BF: the next instruction; BT/S, BF/S: after the slot
        elif form.br in ('uncond', 'bsr'):
            f['d'] = 0
    pos = slot_pos(n)
    pcs = {'A': L + 2 * pos['A'], 'B': L + 2 * pos['B']}
    # memory targets
    targets = {}
    for side, form in (('A', A), ('B', B)):
        for i, m in enumerate(form.mems):
            targets[(side, i)] = mem_target(lay, side, i, m, bool(form.flags.get('hardmem') or form.flags.get('gbrmem')))
    if var and var[0] == 'mem':
        _, ia, ib, how = var
        ta, mb = targets[('A', ia)], B.mems[ib]
        tb = ta if how == 'same' else (ta & ~31) | ((ta + 8) & 31)
        if mb.size in (2, 4, 8):
            tb &= ~(mb.size - 1)
        targets[('B', ib)] = tb
    fixed = set()
    for side, form, f in (('A', A, fa), ('B', B, fb)):
        pc = pcs[side]
        for i, m in enumerate(form.mems):
            t = targets[(side, i)]
            if m.mode == 'pc':
                tgt = lay.POOL + 4 + 4 * i
                d = (tgt - (pc + 4)) // 2 if m.size == 2 else (tgt - ((pc & ~3) + 4)) // 4
                if not 0 <= d <= 255:
                    raise Bad('literal pool out of PC-relative reach')
                f['d'] = d
                continue
            if m.mode == 'gbr':
                d = (t - lay.GBR) // m.size
                if not 0 <= d <= 255:
                    raise Bad('GBR displacement out of range')
                f['d'] = d
                continue
            if m.mode == 'gbr0':
                if 'r0' not in fixed:
                    st.reg['r0'] = t - lay.GBR
                    fixed.add('r0')
                continue
            base = f'r{f[m.base[1]]}'
            if m.mode == 'disp':
                f['d'] = 1
                bv = t - m.size
            elif m.mode == 'r0':
                if 'r0' not in fixed:
                    st.reg['r0'] = 8
                    fixed.add('r0')
                bv = t - st.reg['r0']
            elif m.mode == 'pre':
                bv = t + m.size
            else:
                bv = t
            if base not in fixed:
                st.reg[base] = bv & M32
                fixed.add(base)
        if form.flags.get('pcrel') and not form.mems:        # MOVA: points into the pool
            d = (lay.POOL + 8 - ((pc & ~3) + 4)) // 4
            if not 0 <= d <= 255:
                raise Bad('MOVA out of reach')
            f['d'] = d
    # control values
    mode_after_a = st.fpscr_mode ^ (1 << 21 if A.name == 'FRCHG' else 0) ^ (1 << 20 if A.name == 'FSCHG' else 0)
    fpscr_at = {'A': st.fpscr_mode, 'B': mode_after_a}
    for side, form, f in (('A', A, fa), ('B', B, fb)):
        pc = pcs[side]
        want = {'zero': 0, 'target': pc + 4, 'sr': lay.SR | T_init, 'vbr': lay.VBR, 'fpscr': fpscr_at[side]}
        for slot, what in form.flags.get('hard', {}).items():
            reg = {'Rn': f'r{f.get("n")}', 'Rm': f'r{f.get("m")}', 'PR': 'pr', 'SPC': 'spc', 'SSR': 'ssr'}[slot]
            st.reg[reg] = want[what]
        if form.flags.get('gbrval'):
            st.reg[f'r{f["m"]}'] = lay.GBR
        hm = form.flags.get('hardmem') or ('gbr' if form.flags.get('gbrmem') else None)
        if hm:
            v = {'sr': lay.SR | T_init, 'vbr': lay.VBR, 'fpscr': fpscr_at[side], 'gbr': lay.GBR}[hm]
            t = targets[(side, 0)]
            for k in range(4):
                st.mem[t + k] = (v >> (8 * k)) & 0xFF
    return st, fa, fb, targets


# ---------------------------------------------------------------------------------------------------------------
def enc_movl_r12(k, rx):
    return 0x5000 | (rx << 8) | (12 << 4) | k          # mov.l @(4k,r12),rx


SPECIAL_RESTORE = {'gbr': 0x4B1E, 'pr': 0x4B2A, 'spc': 0x4B4E, 'ssr': 0x4B3E, 'mach': 0x4B0A, 'macl': 0x4B1A,
                   'src': 0x4B0E, 'vbr': 0x4B2E, 'dbr': 0x4BFA, 'fpul': 0x4B5A}


def restore_code(diffs, frtoggle, sztoggle):
    code, words = [], []

    def slot(v):
        if len(words) >= RESTORE_WORDS:
            raise Bad('too many restore values')
        words.append(v & M32)
        return len(words) - 1

    for res, v in diffs:
        if res.startswith('mem:'):
            off = int(res[4:])
            j = off // 4
            if off % 4 or not HARD_OFF // 4 <= j <= 15:
                raise Bad('memory restore out of r12 reach')
            code += [enc_movl_r12(slot(v), 11), 0x1CB0 | j]          # mov.l r11,@(4j,r12)
        elif res[0] == 'r' and res[1:].isdigit():
            code.append(enc_movl_r12(slot(v), int(res[1:])))
        elif res.startswith('bk'):
            code += [enc_movl_r12(slot(v), 11), 0x4B8E | (int(res[2:]) << 4)]
        elif res in SPECIAL_RESTORE:
            code += [enc_movl_r12(slot(v), 11), SPECIAL_RESTORE[res]]
        else:
            raise Bad(f'no restore for {res}')
    if frtoggle:
        code.append(0xFBFD)
    if sztoggle:
        code.append(0xF3FD)
    return code, words


def decode_restore(w):
    if w == 0xFBFD:
        return BYNAME['FRCHG'], {}
    if w == 0xF3FD:
        return BYNAME['FSCHG'], {}
    if w >> 12 == 0x5:
        return BYNAME['MOV.L @(disp,Rm),Rn'], {'n': (w >> 8) & 15, 'm': (w >> 4) & 15, 'd': w & 15}
    if w >> 12 == 0x1:
        return BYNAME['MOV.L Rm,@(disp,Rn)'], {'n': (w >> 8) & 15, 'm': (w >> 4) & 15, 'd': w & 15}
    for f in FORMS:
        if (w & f.fixed_mask) == f.fixed_bits and f.name.startswith(('LDC Rm', 'LDS Rm')):
            fl = {'m': (w >> 8) & 15}
            if 'n' in f.fields:
                sh, wd = f.fields['n']
                fl['n'] = (w >> sh) & ((1 << wd) - 1)
            return f, fl
    raise ValueError(hex(w))


def simulate(lay, pair, st0, fa, fb, n, restores, T_init, iters=2):
    """two iterations of the body on the model; raises Bad.  Returns (state before the restores of iteration 1,
    live-in set, per-side trace)"""
    A, B = pair.a, pair.b
    L = lay.CODE + pair.align
    seq = [('A', A, fa)] + [None] * n + [('B', B, fb)] + [None] * DRAIN + [('R',) + decode_restore(w) for w in restores]
    if len(seq) + 1 > MAX_BODY:
        raise Bad('body too long')
    st = st0.copy()
    live_in, written, info, pre = set(), set(), {}, None
    mode_fixed = st0.fpscr_mode & ~(3 << 20)          # only FR / SZ may change (FRCHG / FSCHG)
    for it in range(iters):
        pending = None
        in_slot = False
        for i, item in enumerate(seq):
            pc = L + 2 * i
            nxt = None
            slot_now, in_slot = in_slot, False
            if item is not None:
                if item[0] == 'R' and pre is None:
                    pre = st.copy()
                if slot_now and item[0] != 'R' and item[1].name in isa.SLOT_ILLEGAL:
                    raise Bad(f'{item[1].name} in a delay slot')
                st.trace_r, st.trace_w = set(), set()
                nxt = execute(st, item[1], item[2], pc)
                # a delayed branch makes the next instruction a delay slot whether or not it is taken
                in_slot = bool(item[0] != 'R' and item[1].delayed)
                # the state the next instruction runs in must stay sane right away, not only after the restores
                if st.reg.get('src') is None or st.reg['src'] & ~0x303 != lay.SR:
                    raise Bad('SR control bits changed')
                if st.reg.get('vbr') != lay.VBR:
                    raise Bad('VBR changed')
                if st.fpscr_mode & ~(3 << 20) != mode_fixed:
                    raise Bad('FPSCR mode changed')
                if it == 0 and item[0] in 'AB':
                    live_in |= st.trace_r - written
                    written |= st.trace_w
                    info[item[0] + '_r'], info[item[0] + '_w'] = set(st.trace_r), set(st.trace_w)
            if pending is not None:
                if nxt is not None:
                    raise Bad('branch in a delay slot')
                if pending != L + 2 * (i + 1):
                    raise Bad(f'branch target {pending:08x} is not the instruction after the delay slot')
                pending = None
            elif nxt is not None:
                tgt, delayed = nxt
                if delayed:
                    pending = tgt
                elif tgt != pc + 2:
                    raise Bad(f'branch target {tgt:08x} is not the next instruction')
        if pending is not None:
            raise Bad('delayed branch at the end of the body')
        if pre is None:
            pre = st.copy()
        if st.reg.get('src') is None or st.reg['src'] & ~0x303 != lay.SR:
            raise Bad('SR control bits changed')
        if st.reg.get('vbr') != lay.VBR:
            raise Bad('VBR changed')
        st.reg['t'] = T_init          # dt r13 ; bf/s L ; sett|nop
    return pre, live_in, info


def checked_regs(form, f):
    w = set()
    for m in form.mems:
        if m.base in ('Rn', 'Rm'):
            w.add(f'r{f[m.base[1]]}')
        if m.mode in ('r0', 'gbr0'):
            w.add('r0')
        if m.mode in ('gbr', 'gbr0'):
            w.add('gbr')
    for slot in form.flags.get('hard', {}):
        w.add({'Rn': f'r{f.get("n")}', 'Rm': f'r{f.get("m")}', 'PR': 'pr', 'SPC': 'spc', 'SSR': 'ssr'}[slot])
    if form.flags.get('gbrval'):
        w.add(f'r{f["m"]}')
    if form.br in ('cond_t', 'cond_f'):
        w.add('t')
    return w


def watched(pair, fa, fb, info, live_in):
    A, B = pair.a, pair.b
    w = set()
    for side, form, f in (('A', A, fa), ('B', B, fb)):
        for m in form.mems:
            if m.base in ('Rn', 'Rm'):
                w.add(f'r{f[m.base[1]]}')
            if m.mode in ('r0', 'gbr0'):
                w.add('r0')
            if m.mode in ('gbr', 'gbr0'):
                w.add('gbr')
        for slot in form.flags.get('hard', {}):
            w.add({'Rn': f'r{f.get("n")}', 'Rm': f'r{f.get("m")}', 'PR': 'pr', 'SPC': 'spc', 'SSR': 'ssr'}[slot])
        if form.flags.get('gbrval'):
            w.add(f'r{f["m"]}')
    if info.get('A_w', set()) & checked_regs(B, fb):
        w |= {r for r in info.get('A_r', set()) if r != 'mem'}      # A feeds a checked slot of B
    w |= {'gbr', 'vbr', 'src'}
    w.discard('t')
    return {r for r in w if r in live_in or r in ('gbr', 'vbr', 'src')}


def find_restores(lay, pair, st0, fa, fb, n, T_init, targets):
    pre, live_in, info = simulate(lay, pair, st0, fa, fb, n, [], T_init, iters=1)
    diffs = []
    for r in sorted(watched(pair, fa, fb, info, live_in)):
        a, b = st0.reg.get(r), pre.reg.get(r)
        if r == 'src':
            a, b = a & ~0x303, (b & ~0x303) if b is not None else None
        if a != b and not (r.startswith('f')):
            if a is None:
                raise Bad(f'{r} needs restoring but has no known value')
            diffs.append((r, st0.reg[r] if r != 'src' else st0.reg['src']))
    hard_targets = {t for t in targets.values() if lay.S + HARD_OFF <= t < lay.S + GEN_LO}
    for t in sorted(hard_targets):
        a = [st0.mem.get(t + k) for k in range(4)]
        b = [pre.mem.get(t + k) for k in range(4)]
        if a != b:
            diffs.append((f'mem:{t - lay.S}', sum(x << (8 * k) for k, x in enumerate(a))))
    frt = ((pre.fpscr_mode ^ st0.fpscr_mode) >> 21) & 1
    szt = ((pre.fpscr_mode ^ st0.fpscr_mode) >> 20) & 1
    code, words = restore_code(diffs, frt, szt)
    st1 = st0.copy()
    for k, v in enumerate(words):
        for j in range(4):
            st1.mem[lay.S + RESTORE_OFF + 4 * k + j] = (v >> (8 * j)) & 0xFF
    simulate(lay, pair, st1, fa, fb, n, code, T_init)
    return st1, code, words


def candidates(reqs):
    out = []
    for req in reqs:
        req &= M32
        for v in (req, 0, 1, 2, 4, 8, M32, 0x80000000, 0x7FFFFFFF, ~req, -req, req - 1, req + 1, req >> 1, req << 1,
                  (req << 16) | (req >> 16), (req & 0xFFFF0000) | ((req & 0xFF) << 8) | ((req >> 8) & 0xFF),
                  (req >> 1) | ((req & 1) << 31), (req << 1) | (req >> 31), req << 16, req >> 16, req << 8, req >> 8,
                  req << 2, req >> 2, req + 4, req - 4, req + 8, req - 8):
            v &= M32
            if v not in out:
                out.append(v)
    return out


def solve(lay, pair, st0, fa, fb, n, T_init, targets):
    """A's result feeds a checked slot of B: search A's data inputs"""
    A = pair.a
    L = lay.CODE + pair.align
    s = st0.copy()
    s.trace_r = set()
    try:
        execute(s, A, fa, L)
    except Bad:
        pass
    bases = {f'r{fa[m.base[1]]}' for m in A.mems if m.base in ('Rn', 'Rm')}
    vars_ = [r for r in sorted(s.trace_r) if r != 'mem' and r not in bases and r not in ('src', 'vbr', 'sgr', 'fpscr')
             and r[:2] not in ('fa', 'fb')]
    memaddrs = []
    for m in A.mems:
        if m.kind in ('L', 'M', 'T'):
            try:
                a, _ = isa.ea(st0.copy(), A, m, fa, L)
                memaddrs.append(a)
            except Bad:
                pass
    posB = L + 2 * (1 + n)
    reqs = [lay.S + GEN_LO + 128, lay.S + GEN_LO + 128 - 4, lay.S + GEN_LO + 128 + 4, 8, posB + 4, 0,
            lay.SR, lay.VBR, lay.GBR, st0.fpscr_mode, 1]
    cands = candidates(reqs)
    tries = []
    for r in sorted(bases):
        b0 = st0.reg.get(r)
        if b0 is not None:
            for dlt in (-1, -2, -4, -8, 1, 2, 4, 8, -3, 3):
                tries.append(({r: (b0 + dlt) & M32}, None, None))
    flagc = {'t': [0, 1], 's': [0, 1], 'qm': [0, 1, 2, 3]}
    for v in vars_:
        for c in flagc.get(v, cands):
            tries.append(({v: c}, None, None))
    if 'i' in A.fields:
        for c in (0, 8, 4, 1, 0xFF, 0x80, 2, 0x10):
            tries.append(({}, c, None))
    for a in memaddrs:
        for c in cands:
            tries.append(({}, None, (a, c)))
    for v1, v2 in itertools.combinations(vars_, 2):
        tries.append(({v2: st0.reg.get(v1)}, None, None))
        if v1 not in flagc and v2 not in flagc:
            for req in reqs[:6]:
                req &= M32
                for c1, c2 in ((req >> 16, req << 16), (req << 16, req >> 16), (req, 0), (0, req), (req, req)):
                    tries.append(({v1: c1 & M32, v2: c2 & M32}, None, None))
        for c1 in flagc.get(v1, cands[:14]):
            for c2 in flagc.get(v2, cands[:14]):
                tries.append(({v1: c1, v2: c2}, None, None))
    for regs, imm, mem in tries:
        s = st0.copy()
        f2 = dict(fa)
        for r, v in regs.items():
            s.reg[r] = v
        if imm is not None:
            f2['i'] = imm
        if mem is not None:
            a, c = mem
            for k in range(8):
                s.mem[a + k] = (c >> (8 * (k % 4))) & 0xFF
        try:
            st1, code, words = find_restores(lay, pair, s, f2, fb, n, T_init, targets)
            return st1, f2, code, words
        except Bad:
            continue
    return None


def build(lay, pair, fa0, fb0, var, n, T_init):
    st0, fa, fb, targets = setup_case(lay, pair, fa0, fb0, var, n, T_init)
    try:
        st1, code, words = find_restores(lay, pair, st0, fa, fb, n, T_init, targets)
    except Bad as e:
        r = solve(lay, pair, st0, fa, fb, n, T_init, targets)
        if r is None:
            raise e
        st1, fa, code, words = r
    return {'st': st1, 'fa': fa, 'fb': fb, 'rest': code, 'words': words, 'n': n, 'T': T_init}


POS_DEP = ('jmp', 'jsr', 'rts', 'rte')


def make_case(lay, pair, var):
    """records (one per distinct field / value set) covering N_LIST"""
    A, B = pair.a, pair.b
    want = var if var and var[0] != 'mem' else None
    al = allocate(pair, want)
    if al is None:
        raise Bad('no register allocation realizes this variant')
    fa0, fb0 = al
    T_init = 0
    if A.br in ('cond_t', 'cond_f'):
        T_init = int(A.flags['want_t'])
    elif B.br in ('cond_t', 'cond_f'):
        T_init = int(B.flags['want_t'])
    pos_dep = bool(B.flags.get('pcrel') or B.br in POS_DEP)
    recs, errs = [], {}
    if pos_dep:
        for n in N_LIST:
            try:
                r = build(lay, pair, fa0, fb0, var, n, T_init)
                r['ns'] = [n]
                recs.append(r)
            except Bad as e:
                errs[n] = str(e)
    else:
        good = None
        for n in (1, 40, 0):
            try:
                r = build(lay, pair, fa0, fb0, var, n, T_init)
                if good is None:
                    good = r
                elif n == 0 and (r['fa'] != good['fa'] or r['rest'] != good['rest']):
                    good = None     # n = 0 needs something else: time it as its own record
                    raise Bad('n = 0 differs')
            except Bad as e:
                errs[n] = str(e)
        if good is not None:
            ns = [x for x in N_LIST if x != 0 or 0 not in errs]
            good['ns'] = ns
            recs.append(good)
        elif 1 in errs or 40 in errs:
            pass
    if not recs:
        raise Bad('; '.join(f'n={k}: {v}' for k, v in sorted(errs.items())))
    return recs, errs


# ---------------------------------------------------------------------------------------------------------------
REGID = {f'r{k}': k for k in range(13)}
SPECIAL_IDS = {'sr': 16, 'gbr': 17, 'ssr': 18, 'spc': 19, 'dbr': 20, 'mach': 21, 'macl': 22, 'pr': 23, 'fpul': 24}


def init_list(lay, st):
    d = initial_state(lay, 'M0', 0)
    out = []
    for k in range(13):
        v = st.reg.get(f'r{k}')
        if v is not None and v != d.reg[f'r{k}']:
            out.append((k, v & M32))
    sr = (st.reg['src'] & ~0x303 & M32) | st.reg['t'] | (st.reg['s'] << 1) | (st.reg['qm'] << 8)
    out.append((16, sr))
    for nm in ('gbr', 'ssr', 'spc', 'dbr', 'mach', 'macl', 'pr', 'fpul'):
        v = st.reg.get(nm)
        out.append((SPECIAL_IDS[nm], (v if v is not None else 0) & M32))
    for k in range(8):
        v = st.reg.get(f'bk{k}')
        if v is not None and v != d.reg[f'bk{k}']:
            out.append((32 + k, v & M32))
    for b in (0, 1):
        for k in range(16):
            v = st.reg.get(fr_name(b, k))
            if v is not None and v != d.reg[fr_name(b, k)]:
                out.append((64 + 16 * b + k, v & M32))
    return out


def mem_list(lay, st, words):
    d = initial_state(lay, 'M0', 0)
    out = [(0, RESTORE_OFF + 4 * k, v) for k, v in enumerate(words)]
    for off in range(HARD_OFF, SCRATCH_SIZE, 4):
        a = lay.S + off
        cur = [st.mem.get(a + j) for j in range(4)]
        if cur != [d.mem.get(a + j) for j in range(4)] and None not in cur:
            out.append((0, off, sum(b << (8 * j) for j, b in enumerate(cur))))
    for off in range(4, POOL_SIZE, 4):
        a = lay.POOL + off
        cur = [st.mem.get(a + j) for j in range(4)]
        if cur != [d.mem.get(a + j) for j in range(4)] and None not in cur:
            out.append((1, off, sum(b << (8 * j) for j, b in enumerate(cur))))
    return out


def pack_case(case_id, pair, rec, lay):
    st = rec['st']
    code = [pair.a.encode(rec['fa']), pair.b.encode(rec['fb'])] + list(rec['rest'])
    init = init_list(lay, st)
    mem = mem_list(lay, st, rec['words'])
    ns = rec['ns']
    tfix = 0x0018 if rec['T'] else 0x0009
    body = struct.pack('<IIHBB', case_id, st0_fpscr(pair), tfix, pair.align, len(ns))
    body += bytes(ns) + bytes((-len(ns)) % 4)
    body += struct.pack('<BBBB', len(rec['rest']), len(init), len(mem), 0)
    body += struct.pack(f'<{len(code)}H', *code) + bytes((len(code) * 2) % 4)
    for rid, v in init:
        body += struct.pack('<BBHI', rid, 0, 0, v)
    for space, off, v in mem:
        body += struct.pack('<BBHI', space, 0, off, v)
    return struct.pack('<HH', len(body) + 4, 0) + body, code


def st0_fpscr(pair):
    pr, sz = MODES[pair.mode]
    return FPSCR_BASE | (pr << 19) | (sz << 20)


def pairs_for(mode):
    fm = isa.forms_in(mode)
    pr, sz = MODES[mode]
    other = isa.MODE_OF.get((pr, 1 - sz))
    only = {f.name for f in fm if f.modes == [mode]}
    for a in fm:
        bset = isa.forms_in(other) if a.name == 'FSCHG' else fm
        for b in bset:
            if mode == 'M0' or a.name in only or b.name in only or a.name == 'FSCHG':
                yield a, b


_LAY = {}


def gen_pair(job):
    an, bn, m, al, probe = job
    if probe not in _LAY:
        _LAY[probe] = Layout(read_probe(probe))
    lay = _LAY[probe]
    a, b = BYNAME[an], BYNAME[bn]
    pair = Pair(a, b, m, al)
    out, skips, seen = [], [], set()
    for vname, var in variants(pair):
        try:
            recs, errs = make_case(lay, pair, var)
        except Bad as e:
            skips.append({'a': an, 'b': bn, 'mode': m, 'var': vname, 'align': al, 'why': str(e)})
            continue
        sig = tuple((a.encode(r['fa']), b.encode(r['fb']), tuple(r['ns'])) for r in recs)
        if sig in seen:
            continue
        seen.add(sig)
        meta = {'a': an, 'b': bn, 'mode': m, 'var': vname, 'align': al, 'nerr': {str(k): v for k, v in errs.items()}}
        out.append((meta, [(pair, r) for r in recs]))
    return out, skips


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--probe', default=os.path.join(HERE, 'runner', 't2run.elf'), help='runner ELF or probe.txt')
    ap.add_argument('--out', default=os.path.join(HERE, 'tests'))
    ap.add_argument('--chunk', type=int, default=1500)
    ap.add_argument('--modes', default='M0,M1,M2')
    ap.add_argument('--pairs', default=None, help='"A|B|M0;..." (pilot)')
    ap.add_argument('--align', default='0')
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--jobs', type=int, default=os.cpu_count())
    args = ap.parse_args()
    lay = Layout(read_probe(args.probe))
    os.makedirs(args.out, exist_ok=True)
    for f in os.listdir(args.out):
        if f.startswith('chunk_'):
            os.remove(os.path.join(args.out, f))
    meta_f = open(os.path.join(args.out, 'cases.jsonl'), 'w')
    skip_f = open(os.path.join(args.out, 'cases_skipped.jsonl'), 'w')
    aligns = [int(x) for x in args.align.split(',')]
    chunk, chunk_no, rec_id, ncase, nskip = [], 0, 0, 0, 0

    def flush():
        nonlocal chunk, chunk_no
        if chunk:
            hdr = struct.pack('<4sIIIIIIIII', b'T2CH', 1, len(chunk), lay.S, lay.CODE, lay.EXIT, lay.SR, lay.VBR, DRAIN, 0)
            with open(os.path.join(args.out, f'chunk_{chunk_no:05d}.bin'), 'wb') as fo:
                fo.write(hdr + b''.join(chunk))
            chunk_no += 1
            chunk = []

    if args.pairs:
        todo = [(a, b, m) for a, b, m in (p.split('|') for p in args.pairs.split(';'))]
    else:
        todo = [(a.name, b.name, m) for m in args.modes.split(',') for a, b in pairs_for(m)]
    if args.limit:
        todo = todo[:args.limit]
    jobs = [(a, b, m, al, args.probe) for a, b, m in todo for al in aligns]
    with multiprocessing.Pool(args.jobs) as pool:
        for out, skips in pool.imap(gen_pair, jobs, chunksize=8):
            for sk in skips:
                skip_f.write(json.dumps(sk) + '\n')
                nskip += 1
            for meta, rec_parts in out:
                for part in rec_parts:
                    pair, r = part
                    blob, code = pack_case(rec_id, pair, r, lay)
                    chunk.append(blob)
                    m2 = dict(meta)
                    m2.update({'id': rec_id, 'case': ncase, 'ns': r['ns'], 'T': r['T'], 'code': [f'{w:04x}' for w in code]})
                    meta_f.write(json.dumps(m2) + '\n')
                    rec_id += 1
                    if len(chunk) >= args.chunk:
                        flush()
                ncase += 1
    flush()
    print(f'{ncase} cases, {rec_id} records, {chunk_no} chunks, {nskip} variants skipped', file=sys.stderr)


if __name__ == '__main__':
    main()
