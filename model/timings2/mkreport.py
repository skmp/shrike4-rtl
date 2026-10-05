#!/usr/bin/env python3
"""mkreport.py [main pass] [bracket pass] [freeze-reg pass] [freeze-fpu pass] -- self-contained HTML report of the
sweep in report/ (index.html + forms/<form>.html).  Everything in it is read from the measured curves; the manual's
tables are not used.

Read-out rules (the same as analyze.py):
    T(n)  cycles per iteration with n pads between A and B;  S(n) = T(n) - n;  P(n) = S(n) - S(40)
    cost(X) = S(40) of (X, NOP) - S(40) of (NOP, NOP)          cycles X adds to the `add` pad stream
    D = cost(A) + n + P(n) at the first n >= 1 with P(n) > 0   earliest issue of B counted from A's issue
    Q = P_brk(0) + cost(A) + cost(B)  (bracket pass)          1: A and B issued in the same cycle, 2: not
"""
import collections
import html
import json
import os
import re
import sys
import time

import analyze
import isa
import t2res

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'report')
MODES = ('M0', 'M1', 'M2')
MODE_DESC = {'M0': 'FPSCR.PR = 0, SZ = 0 (single precision, 32-bit FMOV)',
             'M1': 'FPSCR.PR = 0, SZ = 1 (64-bit FMOV: DR / XD pairs)',
             'M2': 'FPSCR.PR = 1, SZ = 0 (double precision)'}
NS = list(range(17))
MEMKIND = {'L': 'load', 'S': 'store', 'M': 'read-modify-write', 'T': 'TAS.B', 'P': 'PREF', 'I': 'OCBI', 'B': 'OCBP',
           'W': 'OCBWB', 'A': 'MOVCA.L'}


def esc(s):
    return html.escape(str(s))


def fmt(v):
    if v is None:
        return ''
    if abs(v - round(v)) < 1e-6:
        return str(int(round(v)))
    return f'{v:.2f}'


def pstr(P, ns=NS):
    return ' '.join(('·' if abs(P[n]) < 1e-6 else fmt(P[n])).rjust(2) if n in P else ' -' for n in ns)


def slug(name):
    s = name.replace('@', 'at_').replace('+', '_inc').replace('-', 'dec_').replace('#', 'imm_')
    return re.sub(r'[^A-Za-z0-9]+', '_', s).strip('_')


# ---------------------------------------------------------------------------------------------------------------
# the drawing: issue stage + pairing matrix, then one lane per representative form: issue occupancy, result
# latencies (blue ticks, by output operand and consumer kind) and structural holds (orange ticks), all from the curves
REPS = [('M0', ['ADD Rm,Rn', 'SHAD Rm,Rn', 'ADDC Rm,Rn', 'DT Rn', 'CMP/EQ Rm,Rn', 'MOV Rm,Rn', 'MOV.L @Rm,Rn',
                'MOV.B @Rm,Rn', 'MOV.L Rm,@Rn', 'FMOV.S @Rm,FRn', 'FMOV.S FRm,@Rn', 'FMOV FRm,FRn', 'LDS Rm,FPUL',
                'STS FPUL,Rn', 'PREF @Rn', 'OCBWB @Rn', 'MOVCA.L R0,@Rn', 'TAS.B @Rn',
                'FADD FRm,FRn', 'FMUL FRm,FRn', 'FMAC FR0,FRm,FRn', 'FDIV FRm,FRn', 'FSQRT FRn', 'FSRRA FRn',
                'FIPR FVm,FVn', 'FTRV XMTRX,FVn', 'FSCA FPUL,DRn', 'FTRC FRm,FPUL', 'FLOAT FPUL,FRn', 'FCMP/EQ FRm,FRn',
                'MUL.L Rm,Rn', 'DMULS.L Rm,Rn', 'MAC.L @Rm+,@Rn+', 'STS MACL,Rn', 'CLRMAC', 'LDS Rm,MACL',
                'LDS Rm,FPSCR', 'STS FPSCR,Rn', 'LDC Rm,SR', 'STC SR,Rn', 'LDTLB',
                'BT (not taken)', 'BT (taken)', 'BT/S (taken)', 'BRA label', 'BSR label', 'JMP @Rm', 'RTS', 'RTE']),
        ('M2', ['FADD DRm,DRn', 'FMUL DRm,DRn', 'FDIV DRm,DRn', 'FSQRT DRn', 'FCNVSD FPUL,DRn', 'FCNVDS DRm,FPUL']),
        ('M1', ['FMOV @Rm,DRn', 'FMOV DRm,@Rn', 'FMOV DRm,DRn'])]
ABBR = {'FP arithmetic': 'FP', 'FP move': 'FPmv', 'FP load/store': 'FPls', 'load/store': 'ls', 'integer ALU': 'int',
        'control register / misc': 'ctl', 'branch': 'br'}


def svg_pipeline(R, summ, cost, cls, cls_of, ctable, cname, ambiguous):
    W = 1420
    x0, cw, ncyc, rowh = 330, 40, 15, 22
    S = []
    ALL = set(ABBR)

    def X(c):
        return x0 + c * cw

    def txt(x, y, s, size=11, anchor='start', fill='#222', weight='normal', family='sans-serif'):
        S.append(f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" fill="{fill}" font-weight="{weight}" '
                 f'font-family="{family}">{esc(s)}</text>')

    def box(x, y, w, h, fill, stroke='#555'):
        S.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" stroke="{stroke}" rx="3"/>')

    def arrow(x1, y1, x2, y2):
        S.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="#555" marker-end="url(#ah)"/>')

    # measured numbers for the unit boxes
    def Dto(a, opn, cats=ALL, md='M0'):
        s_ = summ.get((a, md))
        if not s_ or opn not in s_['lat']:
            return '–'
        ds = sorted({d for d, b, v in s_['lat'][opn] if category(b) in cats})
        return '/'.join(fmt(d) for d in ds) if ds else '–'

    def Dmost(a, opn, cats=ALL, md='M0'):
        s_ = summ.get((a, md))
        if not s_ or opn not in s_['lat']:
            return '–'
        ds = [d for d, b, v in s_['lat'][opn] if category(b) in cats]
        return fmt(collections.Counter(ds).most_common(1)[0][0]) if ds else '–'

    def hold(a, cats=ALL, md='M0'):
        s_ = summ.get((a, md))
        if not s_:
            return '?'
        ds = sorted({d for d, bs in s_['blocks'].items() if any(category(b) in cats for b in bs)})
        return '/'.join(fmt(d) for d in ds) if ds else 'none'

    def C(a, md='M0'):
        return fmt(cost.get((a, md))) if (a, md) in cost else '?'

    def clsname(cats):
        return ', '.join(cname[i] for i, g in enumerate(cls) if collections.Counter(category(x) for x in g).most_common(1)[0][0] in cats)

    # ---- panel 1: issue stage
    txt(10, 18, 'Issue stage: two instructions per cycle, by measured class (bracket pass, M0)', 13, weight='bold')
    box(10, 40, 110, 40, '#eee')
    txt(65, 64, 'fetch / decode', 11, 'middle')
    arrow(120, 60, 150, 60)
    box(150, 30, 150, 60, '#e8eef8')
    txt(225, 52, 'issue', 12, 'middle', weight='bold')
    txt(225, 68, 'slot 1 + slot 2', 11, 'middle')
    txt(225, 82, 'per cycle', 11, 'middle')
    ncl = len(cls)
    by = 30
    fills = ['#dff0d8', '#d9edf7', '#f2dede', '#fcf8e3', '#e8daef', '#eee', '#eee']
    for i, g in enumerate(cls):
        y = by + i * 38
        cats = collections.Counter(category(x) for x in g)
        desc = ', '.join(f'{ABBR[c]} {k}' for c, k in cats.most_common(3))
        selfp = ctable[i][i][0] and not ctable[i][i][1]
        box(360, y, 340, 30, fills[i % len(fills)])
        txt(368, y + 13, f'{cname[i]}: {len(g)} forms ({desc})', 11, weight='bold')
        with_ = [cname[j] for j in range(ncl) if j != i and ctable[i][j][0] and not ctable[i][j][1]]
        txt(368, y + 26, ('pairs with itself; ' if selfp else 'not with itself; ')
            + ('co-issues with ' + ', '.join(with_) if with_ else 'co-issues with nothing: always issues alone'), 10, fill='#444')
        arrow(300, 60, 358, y + 15)
    nocls = [f for (f, m) in cost if m == 'M0' and f not in cls_of]
    y = by + ncl * 38
    box(360, y, 340, 30, '#eee')
    txt(368, y + 13, f'no class: {len(nocls)} forms', 11, weight='bold')
    txt(368, y + 26, 'cost > 1 (multi-cycle / redirect) or every pair stalls: issues alone', 10, fill='#444')
    arrow(300, 60, 358, y + 15)
    p1h = y + 40
    mx, my, cs = 780, 44, 30
    txt(mx, 24, 'co-issue: row = first, column = second', 11, weight='bold')
    for j in range(ncl):
        txt(mx + cs * (j + 1) + cs / 2, my + 12, cname[j], 10, 'middle')
    for i in range(ncl):
        txt(mx + cs / 2, my + cs * (i + 1) + 19, cname[i], 10, 'middle')
        for j in range(ncl):
            yv, nv = ctable[i][j]
            f = '#9fd89f' if yv and not nv else '#f0a0a0' if nv and not yv else '#ddd' if (yv or nv) else '#fff'
            box(mx + cs * (j + 1), my + cs * (i + 1), cs, cs, f, '#999')
            txt(mx + cs * (j + 1) + cs / 2, my + cs * (i + 1) + 19,
                'Y' if yv and not nv else '–' if nv and not yv else '?' if (yv or nv) else '', 12, 'middle')
    txt(mx, my + cs * (ncl + 1) + 16, 'Y: every measured pair of the two classes issued in one cycle;  –: none did', 10, fill='#444')
    p1h = max(p1h, my + cs * (ncl + 1) + 30)

    # ---- panel 2: execution units, every figure read from the tables
    y = p1h + 14
    txt(10, y, 'Execution units as the measurements separate them (cycles from issue; M0 unless noted)', 13, weight='bold')
    y += 10
    FP, FPLS, INT, CTL, BR = {'FP arithmetic'}, {'FP load/store'}, {'integer ALU'}, {'control register / misc'}, {'branch'}
    units = [
        ('integer ALU', '#d9edf7', [
            f'issue classes {clsname(INT)}',
            'result → any consumer: no wait seen',
            '(usable by the next issue cycle)',
            f'T → conditional branch {Dto("CMP/EQ Rm,Rn", "T", BR)}',
            f'T → STC SR {Dto("CMP/EQ Rm,Rn", "T", CTL)}',
            f'ADDC/DT T → STC SR {Dto("ADDC Rm,Rn", "T", CTL)}',
            f'load → SHAD/SHLD: hold {hold("MOV.L @Rm,Rn", INT)}']),
        ('load / store', '#dff0d8', [
            f'issue class {clsname({"load/store"})}',
            f'load → use {Dmost("MOV.L @Rm,Rn", "Rn")} (byte {Dmost("MOV.B @Rm,Rn", "Rn")})',
            f'FP load → FP arith {Dto("FMOV.S @Rm,FRn", "FRn", FP)}',
            f'FP load → FP store {Dto("FMOV.S @Rm,FRn", "FRn", FPLS)}',
            f'store → load, same address {Dto("MOV.L Rm,@Rn", "memory")}',
            f'OCBWB → load: hold {hold("OCBWB @Rn", {"load/store"})} (plateau)',
            f'cost: TAS.B {C("TAS.B @Rn")}, OCBI {C("OCBI @Rn")}, OCBP {C("OCBP @Rn")}, PREF {C("PREF @Rn")}']),
        ('multiplier (in the FP pipe)', '#e8daef', [
            f'MUL.L / DMULS.L: cost {C("MUL.L Rm,Rn")} / {C("DMULS.L Rm,Rn")}',
            f'MUL.L → STS MACL {Dto("MUL.L Rm,Rn", "MACL")}',
            f'STS MACL → use {Dmost("STS MACL,Rn", "Rn")}',
            f'CLRMAC → STS MACL {Dto("CLRMAC", "MACL")}',
            f'MUL.L holds FP arith {hold("MUL.L Rm,Rn", FP)}',
            f'FTRV holds MUL {hold("FTRV XMTRX,FVn", INT)}, FSCA {hold("FSCA FPUL,DRn", INT)}',
            'stalls counted by the FPU freeze counter']),
        ('FP pipeline', '#fcf8e3', [
            f'issue class {clsname(FP)}',
            f'FADD/FMUL/FMAC/FLOAT → FP arith {Dmost("FADD FRm,FRn", "FRn", FP)}, store {Dmost("FADD FRm,FRn", "FRn", FPLS)}',
            f'FTRC → FPUL readers {Dto("FTRC FRm,FPUL", "FPUL")}; FPSCR flags {Dto("FADD FRm,FRn", "fpscr.cause")}',
            f'FP move / FLDI → FP arith {Dto("FMOV FRm,FRn", "FRn", FP)}; FLDS → FSCA {Dto("FLDS FRm,FPUL", "FPUL", FP)}',
            f'FIPR {Dto("FIPR FVm,FVn", "FIPRn", FP)}, holds FP {hold("FIPR FVm,FVn", FP)}',
            f'FTRV {Dto("FTRV XMTRX,FVn", "FVn", FP)} by element, holds FP {hold("FTRV XMTRX,FVn", FP)}',
            f'FSCA {Dto("FSCA FPUL,DRn", "DRn", FP)} (sin/cos), holds FP {hold("FSCA FPUL,DRn", FP)}',
            f'FSRRA {Dmost("FSRRA FRn", "FRn", FP)}, holds FP {hold("FSRRA FRn", FP)}',
            f'double: FADD {Dmost("FADD DRm,DRn", "DRn", FP, "M2")}, FMUL {Dmost("FMUL DRm,DRn", "DRn", FP, "M2")}']),
        ('divider / square root', '#f2dede', [
            f'FDIV {Dmost("FDIV FRm,FRn", "FRn", FP)} → FP arith, {Dmost("FDIV FRm,FRn", "FRn", FPLS)} → store',
            f'FSQRT {Dmost("FSQRT FRn", "FRn", FP)} → FP arith, {Dmost("FSQRT FRn", "FRn", FPLS)} → store',
            f'next FDIV/FSQRT after FDIV: {hold("FDIV FRm,FRn", FP)}',
            f'next FDIV/FSQRT after FSQRT: {hold("FSQRT FRn", FP)}',
            'other FP ops not held: own unit',
            f'double FDIV {Dmost("FDIV DRm,DRn", "DRn", FP, "M2")}, FSQRT {Dmost("FSQRT DRn", "DRn", FP, "M2")}',
            f'FPSCR flags {Dto("FDIV FRm,FRn", "fpscr.cause")} (single)']),
        ('control / system registers', '#eee', [
            f'issue class {clsname(CTL)}: issues alone',
            f'LDS FPSCR → FP {Dto("LDS Rm,FPSCR", "fpscr.mode", FP)}; STS FPSCR → use {Dmost("STS FPSCR,Rn", "Rn")}',
            f'LDC SR: cost {C("LDC Rm,SR")}, T → branch {Dto("LDC Rm,SR", "T", BR)}',
            f'LDS FPUL → FP {Dto("LDS Rm,FPUL", "FPUL", FP)}; STS FPUL → use {Dmost("STS FPUL,Rn", "Rn")}',
            f'taken: BRA {C("BRA label")}, BT {C("BT (taken)")}, BT/S {C("BT/S (taken)")}, BSR {C("BSR label")}',
            f'JMP {C("JMP @Rn")}, JSR {C("JSR @Rn")}, RTS {C("RTS")}, RTE {C("RTE")}; not taken BT {C("BT (not taken)")}',
            f'LDTLB cost {C("LDTLB")}; LDC.L SR cost {C("LDC.L @Rm+,SR")}'])]
    bw = (W - 20 - 5 * 8) / 6
    bh = 12 + 12 * max(len(u[2]) for u in units) + 8
    for k, (name, fill, lines) in enumerate(units):
        bx = 10 + k * (bw + 8)
        box(bx, y, bw, bh, fill)
        txt(bx + 6, y + 13, name, 11, weight='bold')
        for l, line in enumerate(lines):
            txt(bx + 6, y + 27 + 12 * l, line, 9.5, fill='#333')
    y += bh + 8
    p2h = y

    # ---- panel 3: lanes
    y = p2h + 14
    txt(10, y, 'Issue → result: cycles from A\'s issue (mode M0 unless noted)', 13, weight='bold')
    y += 8
    txt(10, y + 14, 'form', 10, fill='#666')
    txt(x0 - 70, y + 14, 'class cost', 10, fill='#666')
    for c in range(ncyc + 1):
        txt(X(c), y + 14, str(c), 10, 'middle', fill='#666')
    top = y + 20
    ri = {(md, a): (i, j) for i, (md, lst) in enumerate(REPS) for j, a in enumerate(lst)}
    rows = [(md, a) for md, lst in REPS for a in lst if (a, md) in cost]
    rows.sort(key=lambda t: (t[0] != 'M0', cls_of.get(t[1], 99) if t[0] == 'M0' else 0, ri[t]))
    H = top + len(rows) * rowh + 10
    for c in range(ncyc + 1):
        S.append(f'<line x1="{X(c)}" y1="{top}" x2="{X(c)}" y2="{H}" stroke="{"#bbb" if c % 5 == 0 else "#e6e6e6"}"/>')
    last_group = None
    for k, (md, a) in enumerate(rows):
        yy = top + k * rowh
        grp = (cname[cls_of[a]] if a in cls_of else 'no class') if md == 'M0' else md
        if grp != last_group and k:
            S.append(f'<line x1="10" y1="{yy}" x2="{W - 10}" y2="{yy}" stroke="#bbb"/>')
        last_group = grp
        S.append(f'<rect x="0" y="{yy}" width="{W}" height="{rowh}" fill="{"#fafafa" if k % 2 else "#fff"}" opacity="0.7"/>')
        txt(10, yy + 15, a + (f'  [{md}]' if md != 'M0' else ''), 11, family='ui-monospace, monospace')
        c = cost[(a, md)]
        txt(x0 - 70, yy + 15, f'{grp}  {fmt(c)}', 10, fill='#444')
        ci = max(c, 1)
        wbar = cw * min(ci, ncyc) if c >= 1 else cw * 0.5
        S.append(f'<rect x="{X(0)}" y="{yy + 5}" width="{wbar:.1f}" height="12" fill="{"#444" if c >= 1 else "#9ab"}"/>')
        if ci > ncyc:
            txt(X(ncyc) + 3, yy + 15, '»', 11, fill='#444', weight='bold')
        s_ = summ.get((a, md))
        ticks = []
        if s_:
            for opn, lst in sorted(s_['lat'].items()):
                byd = collections.defaultdict(collections.Counter)
                for d, b, v in lst:
                    byd[d][ABBR[category(b)]] += 1
                for d, cnt in sorted(byd.items()):
                    ticks.append((d, '#2a6fd6', f'{opn} {fmt(d)}→' + '·'.join(f'{kk}{vv}' for kk, vv in cnt.most_common(3))))
            for d, bs in sorted(s_['blocks'].items()):
                cnt = collections.Counter(ABBR[category(b)] for b in bs)
                ticks.append((d, '#e08a00', f'holds {fmt(d)}: ' + '·'.join(f'{kk}{vv}' for kk, vv in cnt.most_common(3))))
        if ticks:
            dmax = max(min(t[0], ncyc) for t in ticks)
            S.append(f'<line x1="{X(ci):.1f}" y1="{yy + 11}" x2="{X(dmax):.1f}" y2="{yy + 11}" stroke="#aaa" stroke-dasharray="3 2"/>')
            for d, col, lab in ticks:
                xd = X(min(d, ncyc))
                S.append(f'<rect x="{xd - 2:.1f}" y="{yy + 3}" width="4" height="16" fill="{col}"/>')
                if d > ncyc:
                    txt(xd + 3, yy + 15, '»', 11, fill=col, weight='bold')
            labs = [lab for _, _, lab in ticks]
            maxch = int((W - 12 - X(dmax) - 8) / (0.5 * 9))
            lines, cur = [], ''
            for lab in labs:
                if cur and len(cur) + 2 + len(lab) > maxch:
                    lines.append(cur)
                    cur = lab
                else:
                    cur = (cur + '; ' + lab) if cur else lab
            lines.append(cur)
            if len(lines) > 2:
                lines = lines[:2]
                lines[1] = lines[1][:maxch - 1] + '…'
            if len(lines) == 1:
                txt(X(dmax) + 8, yy + 15, lines[0], 9, fill='#333')
            else:
                txt(X(dmax) + 8, yy + 10, lines[0], 9, fill='#333')
                txt(X(dmax) + 8, yy + 19.5, lines[1], 9, fill='#333')
        elif c >= 1:
            txt(X(min(ci, ncyc)) + (20 if ci > ncyc else 8), yy + 15, 'no consumer waits at n ≥ 1', 9, fill='#888')
        else:
            txt(X(1) + 8, yy + 15, 'issues alongside a pad; no consumer waits at n ≥ 1', 9, fill='#888')
    y = H + 14
    S.append(f'<rect x="10" y="{y - 9}" width="20" height="10" fill="#444"/>')
    txt(36, y, 'issue occupancy (cost cycles)', 10)
    S.append(f'<rect x="230" y="{y - 9}" width="10" height="10" fill="#9ab"/>')
    txt(246, y, 'cost 0: issues with a pad', 10)
    S.append(f'<rect x="420" y="{y - 9}" width="4" height="12" fill="#2a6fd6"/>')
    txt(430, y, 'result latency D by output operand → consumer kinds (count)', 10)
    S.append(f'<rect x="780" y="{y - 9}" width="4" height="12" fill="#e08a00"/>')
    txt(790, y, 'structural hold: independent consumers cannot issue before D;  » beyond the axis', 10)
    txt(10, y + 16, 'kinds: FP arithmetic, FPmv FP move, FPls FP load/store, ls load/store, int integer ALU (incl. MUL*), '
        'ctl control register, br branch.  In the unit boxes, – means no consumer of that kind waits beyond its '
        'independent pair.', 10, fill='#666')
    Ht = y + 26
    body = '\n'.join(S)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{Ht}" viewBox="0 0 {W} {Ht}" '
            f'font-family="system-ui, sans-serif"><defs><marker id="ah" markerWidth="8" markerHeight="8" refX="7" refY="4" '
            f'orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="#555"/></marker></defs>'
            f'<rect width="{W}" height="{Ht}" fill="#fff"/>\n{body}\n</svg>')

# ---------------------------------------------------------------------------------------------------------------
def load_pass(pas):
    d = os.path.join(HERE, 'results', pas)
    if not os.path.isdir(d):
        return {}, collections.Counter(), 0, None
    recs = t2res.load(pas)
    status = collections.Counter(r['status'] for r in recs.values())
    event = next(iter(recs.values()))['event'] if recs else None
    rows = {}
    for c, e in t2res.curves(recs).items():
        m, pts = e['meta'], e['pts']
        if e['status'] or 40 not in pts:
            continue
        s40 = pts[40][0] - 40
        rows[(m['a'], m['b'], m['mode'], m['var'])] = {
            'a': m['a'], 'b': m['b'], 'mode': m['mode'], 'var': m['var'], 'S40': s40, 'T40': pts[40][0],
            'P': {n: round(pts[n][0] - n - s40, 3) for n in pts if n != 40},
            'ev': {n: round(pts[n][1], 3) for n in pts},
            'integral': all(abs(pts[n][0] - round(pts[n][0])) < 1e-6 for n in pts)}
    return rows, status, len(recs), event


def category(name):
    f = isa.BYNAME[name]
    if f.br:
        return 'branch'
    if name.startswith(('LDC', 'STC', 'LDS', 'STS', 'CLRMAC', 'CLRS', 'SETS', 'CLRT', 'SETT', 'LDTLB', 'RTE', 'NOP')):
        return 'control register / misc'
    if name.startswith('F'):
        if f.is_mem():
            return 'FP load/store'
        if name.startswith(('FMOV', 'FLDI', 'FLDS', 'FSTS', 'FABS', 'FNEG', 'FRCHG', 'FSCHG')):
            return 'FP move'
        return 'FP arithmetic'
    if f.is_mem():
        return 'load/store'
    return 'integer ALU'


def dep_label(a, b, var):
    """what the variant shares: 'RAW Rn>Rm' etc."""
    A, Bf = isa.BYNAME[a], isa.BYNAME[b]
    if var == 'ind':
        return 'independent'
    if var.startswith('imp:'):
        return 'implicit RAW ' + var[4:]
    if var.startswith('ov:'):
        m = re.match(r'ov:([^>]+)>([^\[]+)(\[(\d)\])?', var)
        oa, ob, k = m.group(1), m.group(2), m.group(4)
        acc_a = next((o.acc for o in A.ops if o.name == oa), 'r')
        acc_b = next((o.acc for o in Bf.ops if o.name == ob), 'r')
        kind = 'RAW' if 'w' in acc_a and 'r' in acc_b else 'WAW' if 'w' in acc_a else 'WAR'
        return f'{kind} {oa} → {ob}' + (f' [{k}]' if k else '')
    m = re.match(r'mem:(same|line)(\d)?(\d)?', var)
    ia, ib = int(m.group(2) or 0), int(m.group(3) or 0)
    ka = MEMKIND.get(A.mems[ia].kind, '?') if A.mems else '?'
    kb = MEMKIND.get(Bf.mems[ib].kind, '?') if Bf.mems else '?'
    return f'memory {ka} → {kb}, same {"address" if m.group(1) == "same" else "line"}'


def out_operand(var):
    if var.startswith('imp:'):
        return var[4:]
    if var.startswith('ov:'):
        return var[3:].split('>')[0]
    return 'memory'


# ---------------------------------------------------------------------------------------------------------------
def pairing(R, B, cost):
    Q = {}
    for (a, b, md, v), rb in B.items():
        if v != 'ind' or 0 not in rb['P']:
            continue
        rm = R.get((a, b, md, v))
        ca, cb = cost.get((a, md)), cost.get((b, md))
        if rm is None or ca is None or cb is None:
            continue
        if any(abs(rm['P'].get(n, 0)) > 1e-6 for n in range(1, 17)) or not rb['integral']:
            continue
        if ca > 1 or cb > 1:
            continue
        if analyze.implicit_dep(a, b):
            continue
        Q[(a, b, md)] = (round(rb['P'][0] + ca + cb, 3), round(rb['ev'][0] - rb['ev'][40], 3))
    forms = sorted({a for (a, b, md) in Q if md == 'M0'})
    pairs = {k: v[0] == 1 for k, v in Q.items() if v[0] in (1, 2)}
    bad = {k: v for k, v in Q.items() if v[0] not in (1, 2)}

    def row(x):
        r = {}
        for y in forms:
            if (x, y, 'M0') in pairs:
                r[('a', y)] = pairs[(x, y, 'M0')]
            if (y, x, 'M0') in pairs:
                r[('b', y)] = pairs[(y, x, 'M0')]
        return r
    rows_ = {x: row(x) for x in forms}
    classes = []
    for x in sorted(forms, key=lambda f: -len(rows_[f])):
        rx = rows_[x]
        for cl in classes:
            common = [k for k in rx if k in cl[1]]
            if len(common) >= 8 and all(rx[k] == cl[1][k] for k in common):
                cl[0].append(x)
                cl[1].update(rx)
                break
        else:
            classes.append([[x], dict(rx)])
    ambiguous = {}
    for cl in classes:
        for x in cl[0]:
            rx = rows_[x]
            fits = [j for j, other in enumerate(classes)
                    if all(rx[k] == other[1][k] for k in rx if k in other[1] and k[1] != x)]
            if len(fits) > 1:
                ambiguous[x] = len(fits)
    cls = sorted((sorted(g[0]) for g in classes), key=lambda g: (-len(g), g[0]))
    cls_of = {x: i for i, g in enumerate(cls) for x in g}
    table = []
    for gi in cls:
        cells = []
        for gj in cls:
            vals = [pairs.get((x, y, 'M0')) for x in gi for y in gj]
            yes, no = sum(1 for v in vals if v is True), sum(1 for v in vals if v is False)
            cells.append((yes, no))
        table.append(cells)
    return Q, pairs, bad, cls, cls_of, ambiguous, table


CSS = """
body{font-family:system-ui,-apple-system,sans-serif;font-size:13px;line-height:1.35;margin:0 auto;padding:0 16px 48px;
max-width:1700px;color:#222;background:#fff}
h1{font-size:20px;margin:18px 0 6px}h2{font-size:16px;margin:26px 0 6px;border-bottom:1px solid #ccc;padding-bottom:2px}
h3{font-size:14px;margin:16px 0 4px}
table{border-collapse:collapse;margin:6px 0 10px}
th,td{border:1px solid #d0d0d0;padding:2px 6px;text-align:left;vertical-align:top}
th{background:#f0f0f0;position:sticky;top:0;z-index:1}
tr:nth-child(even) td{background:#fafafa}
td.n{text-align:right;font-variant-numeric:tabular-nums}
.c{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;white-space:pre;font-size:12px}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px}
.small{color:#666;font-size:12px}
.nav a{margin-right:14px}
.y{color:#0a0;font-weight:600}.no{color:#b00}
.tag{display:inline-block;padding:0 5px;border-radius:3px;background:#e8eef8;font-size:11px;margin-right:3px}
.odd{background:#fff4d6 !important}
details{margin:6px 0}summary{cursor:pointer;font-weight:600}
input.f{width:340px;padding:3px 6px;margin:4px 0;font-size:13px}
"""

JS = """
function flt(id, q){q=q.toLowerCase();var rows=document.querySelectorAll('#'+id+' tbody tr');
for(var i=0;i<rows.length;i++){rows[i].style.display=rows[i].textContent.toLowerCase().indexOf(q)>=0?'':'none';}}
"""


def page(title, body, root=''):
    return (f'<!DOCTYPE html><html><head><meta charset="utf-8"><title>{esc(title)}</title>'
            f'<style>{CSS}</style><script>{JS}</script></head><body>{body}</body></html>')


def table(headers, rows, cls_rows=None, tid=None, num_cols=()):
    h = [f'<table{" id=" + chr(34) + tid + chr(34) if tid else ""}><thead><tr>' + ''.join(f'<th>{esc(x)}</th>' for x in headers)
         + '</tr></thead><tbody>']
    for i, r in enumerate(rows):
        cl = f' class="{cls_rows[i]}"' if cls_rows and cls_rows[i] else ''
        h.append(f'<tr{cl}>' + ''.join(f'<td{" class=n" if j in num_cols else ""}>{c}</td>' for j, c in enumerate(r)) + '</tr>')
    h.append('</tbody></table>')
    return '\n'.join(h)


def dcount(ds):
    """'2 ×3, 3 ×40' from a list of D values"""
    c = collections.Counter(ds)
    return ', '.join(f'{fmt(d)} ×{k}' for d, k in sorted(c.items()))


# ---------------------------------------------------------------------------------------------------------------
def main():
    t0 = time.time()
    names = (sys.argv[1:] + ['par', 'brk', 'frzreg', 'frzfpu'])[:4]
    passes = {}
    for nm in names:
        rows, status, nrec, event = load_pass(nm)
        passes[nm] = {'rows': rows, 'status': status, 'nrec': nrec, 'event': event}
        print(f'{nm}: {nrec} records, {len(rows)} curves, status {dict(status)}  ({time.time() - t0:.0f} s)')
    R = passes[names[0]]['rows']
    B = passes[names[1]]['rows']
    Freg, Ffpu = passes[names[2]]['rows'], passes[names[3]]['rows']
    nskip = sum(1 for _ in open(os.path.join(HERE, 'tests', 'cases_skipped.jsonl')))
    skip_why = collections.Counter()
    for line in open(os.path.join(HERE, 'tests', 'cases_skipped.jsonl')):
        w = json.loads(line)['why']
        w = re.sub(r'^n=\d+: ', '', w.split(';')[0])
        w = re.sub(r'[0-9a-f]{8}', '#', w)
        w = re.sub(r'\b\d+\b', '#', w)
        skip_why[w] += 1

    # ---- cost, D, ramp
    # the (NOP, NOP) reference exists in M0 only (M1 / M2 hold the pairs with a form that exists only there); the
    # loop without A and B is the same integer code in every FPU mode
    cost, par_delta = {}, {}
    base = R[('NOP', 'NOP', 'M0', 'ind')]
    for md in MODES:
        base = R.get(('NOP', 'NOP', md, 'ind'), base)
        for (a, b, m, v), r in R.items():
            if m == md and b == 'NOP' and v == 'ind':
                cost[(a, md)] = round(r['S40'] - base['S40'], 3)
                par_delta[(a, md)] = round(r['ev'][40] - base['ev'][40], 3)

    def required(r):
        P = r['P']
        for n in range(1, 17):
            if n in P and P[n] > 1e-6:
                ca = cost.get((r['a'], r['mode']))
                return None if ca is None else round(ca + n + P[n], 3)
        return None

    for r in R.values():
        r['ramp'] = analyze.is_ramp(r['P'])
        r['D'] = required(r)
        r['odd'] = not r['ramp'] or not r['integral']

    # freeze attribution at the first stalled n >= 1
    def freeze(key, r):
        for n in range(1, 17):
            if r['P'].get(n, 0) > 1e-6:
                break
        else:
            return None
        fr, ff = Freg.get(key), Ffpu.get(key)
        if fr is None or ff is None:
            return None
        return (n, r['P'][n], round(fr['ev'].get(n, 0) - fr['ev'][40], 3), round(ff['ev'].get(n, 0) - ff['ev'][40], 3))

    def freeze_label(fz):
        if fz is None:
            return ''
        n, p, a, b = fz
        if abs(a) < 0.05 and abs(b) < 0.05:
            return 'none'
        if a > 0.5 * p and b < 0.05:
            return 'register'
        if b > 0.5 * p and a < 0.05:
            return 'FPU'
        return f'register {a:g} + FPU {b:g}'

    # consumer-side holds: an independent B that waits with the same curve after (nearly) every A
    byb = collections.defaultdict(list)
    for key, r in R.items():
        if key[3] == 'ind' and r['integral']:
            byb[(key[1], key[2])].append(r)
    intrinsic = {}
    for (b, md), lst in byb.items():
        byc = collections.defaultdict(list)
        for r in lst:
            byc[cost.get((r['a'], md))].append(r)
        for ca, l2 in byc.items():
            cnt = collections.Counter(tuple(r['P'].get(n, 0) for n in range(1, 17)) for r in l2 if r['D'] is not None)
            for Pt, k in cnt.items():
                if len(l2) >= 10 and k >= 0.8 * len(l2):
                    intrinsic[(b, md, ca, Pt)] = (k, len(l2))

    # classify rows
    kind = {}
    for key, r in R.items():
        a, b, md, v = key
        if not r['integral']:
            kind[key] = 'noisy'
            if v == 'ind':
                r['imp'] = ''
            else:
                ind = R.get((a, b, md, 'ind'))
                r['Dind'] = ind['D'] if ind else None
        elif v == 'ind':
            ra, wa = analyze.implicit_rw(a)
            rb, wb = analyze.implicit_rw(b)
            raw = sorted((wa & rb) - {'pc'})
            waw = sorted((wa & wb) - {'pc', 'fpscr.cause'})
            war = sorted((ra & wb) - {'pc'})
            r['imp'] = ', '.join(x for x in ['RAW ' + '+'.join(raw) if raw else '', 'WAW ' + '+'.join(waw) if waw else '',
                                             'WAR ' + '+'.join(war) if war else ''] if x)
            Pt = tuple(r['P'].get(n, 0) for n in range(1, 17))
            if r['D'] is None:
                kind[key] = 'flat'
            elif (b, md, cost.get((a, md)), Pt) in intrinsic:
                kind[key] = 'intrinsic'
            elif raw:
                kind[key] = 'implicit'
            else:
                kind[key] = 'structural'
        else:
            ind = R.get((a, b, md, 'ind'))
            dind = ind['D'] if ind else None
            r['Dind'] = dind
            if r['D'] is not None and (dind is None or r['D'] > dind):
                kind[key] = 'latency'
            elif r['D'] is not None:
                kind[key] = 'masked'          # waits no longer than the independent pair
            else:
                kind[key] = 'flat'
        r['kind'] = kind[key]
        r['fz'] = freeze(key, r)

    Q, pairs, bad, cls, cls_of, ambiguous, ctable = pairing(R, B, cost) if B else ({}, {}, {}, [], {}, {}, [])
    cname = {i: f'C{i + 1}' for i in range(len(cls))}

    # per-form summaries
    summ = collections.defaultdict(lambda: {'lat': collections.defaultdict(list), 'blocks': collections.defaultdict(list),
                                            'odd': 0, 'fz': collections.Counter(), 'ncase': 0})
    for key, r in R.items():
        a, b, md, v = key
        s = summ[(a, md)]
        s['ncase'] += 1
        if r['kind'] in ('latency', 'implicit'):
            opn = out_operand(v) if v != 'ind' else '+'.join(sorted((analyze.implicit_rw(a)[1] & analyze.implicit_rw(b)[0]) - {'pc'}))
            s['lat'][opn].append((r['D'], b, v))
            s['fz'][freeze_label(r['fz'])] += 1
        if r['kind'] == 'structural':
            s['blocks'][r['D']].append(b)
        if r['odd']:
            s['odd'] += 1

    forms_by_mode = {md: sorted({a for (a, m) in cost if m == md}) for md in MODES}
    all_forms = sorted({r['a'] for r in R.values()} | {r['b'] for r in R.values()})
    slugs = {f: slug(f) for f in all_forms}
    assert len(set(slugs.values())) == len(slugs), 'slug collision'

    def flink(f, root=''):
        return f'<a href="{root}forms/{slugs[f]}.html">{esc(f)}</a>'

    os.makedirs(os.path.join(OUT, 'forms'), exist_ok=True)

    # =========================================================================================== index
    H = []
    date = time.strftime('%Y-%m-%d')
    ncurves = len(R)
    nramp = sum(1 for r in R.values() if r['ramp'])
    nint = sum(1 for r in R.values() if r['integral'])
    nodd = sum(1 for r in R.values() if r['odd'])
    H.append(f'<h1>SH7091 pipeline timing: every instruction form against every other</h1>')
    H.append(f'<p class="small">Generated {date} by mkreport.py from passes {", ".join(names)} in results/. '
             'Every number on these pages is a measurement or a stated consequence of the read-out rules below; no '
             'figure is taken from the programming manual.</p>')
    H.append('<p class="nav">' + ' '.join(f'<a href="#{s}">{t}</a>' for s, t in [
        ('method', 'Method'), ('shape', 'Drawing'), ('coverage', 'Coverage'), ('classes', 'Issue classes'), ('forms', 'Per-form tables'),
        ('latency', 'Latency tables'), ('structural', 'Structural blocking'), ('intrinsic', 'Consumer-side holds'), ('odd', 'Curves that are not ramps'),
        ('freeze', 'Freeze attribution'), ('skipped', 'Skipped variants')]) + '</p>')

    # ---- method
    H.append('<h2 id="method">Method and read-out rules</h2>')
    H.append('''<p>Each case is one loop, JIT-assembled on the console and timed with the performance counters
(PRFC1 cycles, PRFC0 one event per pass) with interrupts masked:</p>
<pre class="c">L:  A ; add #1,r14 × n ; B ; add #1,r14 × 40 ; restores ; dt r13 ; bf/s L ; &lt;T fix&gt;</pre>
<p>The <span class="mono">add #1,r14</span> pads form a dependent chain, one per cycle, that nothing else touches. Each case
runs 8 and 136 iterations (best of 3 each); <b>T(n)</b> = (min 136-run − min 8-run) / 128 is the exact cycles of one
iteration with n pads between A and B, all fixed overhead cancelled. n runs 0..16, plus 40 as the far reference.</p>
<ul>
<li><b>P(n)</b> = (T(n) − n) − (T(40) − 40): extra cycles when B follows A at distance n, against far apart.
Curves are printed as P(0) P(1) … P(16), a dot for 0.</li>
<li><b>cost(X)</b> = (T(40) − 40) of (X, NOP) minus the same of (NOP, NOP): cycles X adds to the pad stream.
0: X issues in the same cycle as an <span class="mono">add</span>; 1: it cannot; k &gt; 1: k issue cycles, or a k-cycle
redirect for a taken branch.</li>
<li><b>D</b> = cost(A) + n + P(n) at the first n ≥ 1 with P(n) &gt; 0: the earliest cycle B can issue, counted from A's
issue cycle. For a clean ramp every n on the ramp gives the same D. In a dependency variant D is that result's
latency as that consumer sees it; in the independent variant it is a structural limit. No D: nothing binds B at n ≥ 1
(a 1-cycle integer result cannot be seen, the consumer could not have issued earlier anyway).</li>
<li><b>Q</b> (bracket pass): the first pad after B is replaced by <span class="mono">stc dbr,r11</span>, which issues alone
and waits for nothing; then A;B adjacent costs Q = P(0) + cost(A) + cost(B) = 1 when A and B issued in the same
cycle and 2 when not. Evaluated only for single-cycle A and B with no stall at n ≥ 1 and no shared implicit register.</li>
<li><b>Freeze passes</b>: PRFC0 counts cycles the pipeline was frozen by a CPU-register dependency (event 0x28) or by
the FPU (0x29); the extra count at the first stalled n, against n = 40, says what kind of wait a stall is.</li>
</ul>
<p>Variants per ordered pair (A, B): <b>independent</b> (no shared register or memory except what both must use
implicitly: T, FPUL, FPSCR, MACH/MACL, PR, …); <b>RAW / WAW / WAR</b> on each pair of operands that can share a
register (or a k-th aligned sub-vector of a wider one); <b>memory</b> same address / same 32-byte line for each pair of
memory accesses. Every case is executed on a host model first: no exception can occur, no pointer leaves the case's
own 512-byte scratch, caches are warm (the cache-op instructions touch their own lines on purpose).</p>''')

    # ---- drawing
    svg = svg_pipeline(R, summ, cost, cls, cls_of, ctable, cname, ambiguous)
    with open(os.path.join(OUT, 'pipeline.svg'), 'w') as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n' + svg)
    H.append('<h2 id="shape">Pipeline shape (drawing)</h2>')
    H.append('<p>Top: the issue stage as the bracket pass sees it, with the measured co-issue matrix. Bottom: for '
             'representative forms, the issue occupancy (cost), the result latency each output operand has to each '
             'kind of consumer (blue), and structural holds on independent consumers (orange). Also as '
             '<a href="pipeline.svg">pipeline.svg</a>.</p>')
    H.append(f'<div style="overflow-x:auto">{svg}</div>')

    # ---- coverage
    H.append('<h2 id="coverage">Coverage</h2>')
    rows = []
    for nm in names:
        p = passes[nm]
        st = ', '.join(f'{k:#x}: {v}' for k, v in sorted(p['status'].items()) if k) or 'none'
        rows.append([esc(nm), f'{p["event"]:#x}' if p['event'] is not None else '', p['nrec'], len(p['rows']), st])
    H.append(table(['pass', 'PRFC0 event', 'records', 'cases with a complete curve', 'exceptions (status: count)'], rows,
                   num_cols=(2, 3)))
    rows = [[md, esc(MODE_DESC[md]), len(forms_by_mode[md]), sum(1 for r in R.values() if r['mode'] == md)] for md in MODES]
    H.append(table(['mode', 'FPSCR', 'forms measured', 'cases'], rows, num_cols=(2, 3)))
    H.append(f'<p>{ncurves} cases with a complete curve (17 distances each). {nramp} ({100 * nramp / ncurves:.1f} %) are '
             f'exact ramps P(n) = max(0, c − n) from n = 1 on; {nint} are integral at every point. {nodd} curves are '
             f'not a ramp or not integral (<a href="#odd">listed below</a>). {nskip} variants were not generated because '
             f'they cannot be made safe (<a href="#skipped">reasons</a>).</p>')
    nf = len(isa.FORMS)
    H.append(f'<p>Forms: {nf} in isa.py, {len(all_forms)} measured in at least one mode. Not measured: '
             + ', '.join(esc(f.name) for f in isa.FORMS if f.name not in slugs) + ' (TRAPA always traps; SLEEP halts).</p>')

    # ---- classes
    H.append('<h2 id="classes">Issue classes (which instructions issue in the same cycle)</h2>')
    if cls:
        H.append(f'<p>{len(Q)} single-cycle pairs evaluated in the bracket pass (mode M0); {sum(pairs.values())} co-issue, '
                 f'{len(pairs) - sum(pairs.values())} do not, {len(bad)} give a Q that is neither 1 nor 2 (below). '
                 'Forms whose pairing rows, as first and as second instruction, never disagree where both were measured '
                 'form one class. Class names C1… are assigned here by size; the "contents" column describes the members.</p>')
        rows = []
        for i, g in enumerate(cls):
            cats = collections.Counter(category(x) for x in g)
            with_ = [cname[j] for j in range(len(cls)) if ctable[i][j][0] and not ctable[i][j][1]]
            not_ = [cname[j] for j in range(len(cls)) if ctable[i][j][1] and not ctable[i][j][0]]
            rows.append([cname[i], len(g), ', '.join(f'{c} {k}' for c, k in cats.most_common()),
                         ', '.join(with_) or '—', ', '.join(not_) or '—',
                         '<span class="y">yes</span>' if ctable[i][i][0] and not ctable[i][i][1] else '<span class="no">no</span>'])
        H.append(table(['class', 'forms', 'contents', 'co-issues with (as first)', 'never with', 'with itself'], rows, num_cols=(1,)))
        H.append('<h3>Class × class: pairs measured co-issuing / not (first = row, second = column)</h3>')
        rows = []
        for i in range(len(cls)):
            cells = [cname[i]]
            for j in range(len(cls)):
                y, n = ctable[i][j]
                if y and not n:
                    cells.append(f'<span class="y">Y</span> <span class="small">{y}</span>')
                elif n and not y:
                    cells.append(f'<span class="no">–</span> <span class="small">{n}</span>')
                elif y or n:
                    cells.append(f'? <span class="small">{y} Y / {n} –</span>')
                else:
                    cells.append('<span class="small">not measured</span>')
            rows.append(cells)
        H.append(table([''] + [cname[j] for j in range(len(cls))], rows))
        for i, g in enumerate(cls):
            H.append(f'<h3>{cname[i]} ({len(g)} forms)</h3><p>' + ', '.join(
                flink(x) + (f' <span class="tag">not determined: fits {ambiguous[x]} classes</span>' if x in ambiguous else '')
                for x in g) + '</p>')
        not_in = [f for f in forms_by_mode['M0'] if f not in cls_of]
        H.append(f'<h3>No class ({len(not_in)} forms of M0)</h3><p class="small">Multi-cycle or redirecting forms '
                 '(cost &gt; 1), and forms whose every candidate pair carries a stall or an implicit dependency: cost, '
                 'latency and blocking are measured, dual issue is not.</p><p>' + ', '.join(
            f'{flink(x)} <span class="small">cost {fmt(cost[(x, "M0")])}</span>' for x in not_in) + '</p>')
        if bad:
            by = collections.defaultdict(list)
            for (a, b, md), (q, dev) in sorted(bad.items()):
                by[(a, q)].append(b)
            rows = [[flink(a), fmt(q), len(bs), ', '.join(esc(x) for x in bs)] for (a, q), bs in sorted(by.items())]
            H.append('<details><summary>Pairs with Q outside {1, 2} (%d)</summary>' % len(bad)
                     + table(['A', 'Q', 'B count', 'B'], rows, num_cols=(1, 2)) + '</details>')
    else:
        H.append('<p>No bracket pass.</p>')

    # ---- per-form tables
    H.append('<h2 id="forms">Per-form tables</h2>')
    H.append('<p>cost: cycles in the pad stream. Δpar: extra parallel-issue events per iteration of (X, NOP) against '
             '(NOP, NOP) at n = 40 (1 = X issued together with a pad). latency: required issue distance D by output '
             'operand, as "D ×consumers" over the consumers that wait for it (dependency variants that wait longer than '
             'the independent pair, plus implicit registers). blocks: independent consumers that cannot issue before D. '
             'wait counted as: the freeze counter that saw the latency stalls. Click a form for every case.</p>')
    for md in MODES:
        H.append(f'<h3>Mode {md}: {esc(MODE_DESC[md])}</h3>')
        H.append(f'<input class="f" placeholder="filter {md} table" oninput="flt(\'t{md}\', this.value)">')
        rows = []
        for a in forms_by_mode[md]:
            s = summ.get((a, md))
            f = isa.BYNAME[a]
            lat = '; '.join(f'<b>{esc(opn)}</b>: {dcount([d for d, _, _ in lst])}' for opn, lst in sorted(s['lat'].items())) if s else ''
            blk = ', '.join(f'{fmt(d)}: {len(bs)}' for d, bs in sorted(s['blocks'].items())) if s else ''
            fz = ', '.join(f'{k} {v}' for k, v in s['fz'].most_common()) if s else ''
            c = cname[cls_of[a]] if md == 'M0' and a in cls_of else ''
            if a in ambiguous and md == 'M0':
                c += ' ?'
            rows.append([flink(a), f'<span class="mono">{f.enc}</span>', esc(category(a)), c, fmt(cost[(a, md)]),
                         fmt(par_delta[(a, md)]), lat, blk, fz, s['odd'] if s and s['odd'] else '', s['ncase'] if s else 0])
        H.append(table(['form', 'encoding', 'kind', 'class', 'cost', 'Δpar', 'latency by output operand', 'blocks (D: #B)',
                        'wait counted as', 'odd curves', 'cases'], rows, tid=f't{md}', num_cols=(4, 5, 9, 10)))

    # ---- latency tables: producer operand -> D -> consumers, M0 then the other modes' extra forms
    H.append('<h2 id="latency">Latency tables: what each result reaches, and when</h2>')
    H.append('<p>For every producer A and output operand: the consumers grouped by the required issue distance D they '
             'see (dependency variants that wait longer than the independent pair; implicit registers from the '
             'independent variant). Consumers that see the same D as their independent pair are not listed: their wait '
             'is structural, not the data.</p>')
    for md in MODES:
        H.append(f'<h3>Mode {md}</h3>')
        H.append(f'<input class="f" placeholder="filter {md} latency table" oninput="flt(\'l{md}\', this.value)">')
        rows = []
        for a in forms_by_mode[md]:
            s = summ.get((a, md))
            if not s or not s['lat']:
                continue
            for opn, lst in sorted(s['lat'].items()):
                byd = collections.defaultdict(list)
                for d, b, v in lst:
                    byd[d].append((b, v))
                for d, bs in sorted(byd.items()):
                    bs.sort()
                    cons = ', '.join(esc(b) + ' <span class="small">' + esc(
                        v[3:].split('>')[1] if v.startswith('ov:') else 'implicit' if v == 'ind' else dep_label(a, b, v))
                        + '</span>' for b, v in bs)
                    rows.append([flink(a), esc(opn), fmt(d), len(bs), cons])
        H.append(table(['A (producer)', 'output', 'D', '#', 'consumers B (and the operand or resource that reads it)'],
                       rows, tid=f'l{md}', num_cols=(2, 3)))

    # ---- structural
    H.append('<h2 id="structural">Structural blocking (independent pairs that still wait)</h2>')
    H.append('<p>Independent variants with a D: B cannot issue before D after A although it reads nothing A writes. '
             'Rows whose implicit resources overlap (WAW / WAR on T, FPSCR mode bits, …) are marked.</p>')
    rows = []
    for md in MODES:
        for a in forms_by_mode[md]:
            s = summ.get((a, md))
            if not s or not s['blocks']:
                continue
            for d, bs in sorted(s['blocks'].items()):
                items = []
                for b in sorted(bs):
                    r = R[(a, b, md, 'ind')]
                    items.append(esc(b) + (f' <span class="small">[{esc(r["imp"])}]</span>' if r.get('imp') else '')
                                 + ('' if r['ramp'] else ' <span class="tag">not a ramp</span>'))
                rows.append([md, flink(a), fmt(d), len(bs), ', '.join(items)])
    H.append(f'<input class="f" placeholder="filter" oninput="flt(\'st\', this.value)">')
    H.append(table(['mode', 'A', 'D', '#', 'B (blocked)'], rows, tid='st', num_cols=(2, 3)))

    # ---- consumer-side holds
    H.append('<h2 id="intrinsic">Consumer-side holds</h2>')
    H.append('<p>Forms that, as the second instruction, wait with the same curve after at least 80 % of all first '
             'instructions of the same cost: a property of B alone, not of the pair. These curves are left out of the '
             'structural table above, the per-form blocks and the drawing.</p>')
    rows = []
    for (b, md, ca, Pt), (k, tot) in sorted(intrinsic.items(), key=lambda kv: (kv[0][1], kv[0][0], kv[0][2])):
        P = dict(zip(range(1, 17), Pt))
        d = next(n + P[n] for n in range(1, 17) if P[n] > 1e-6)
        rows.append([md, flink(b), fmt(ca), f'<span class="c">{pstr(P, list(range(1, 17)))}</span>', fmt(ca + d), f'{k} / {tot}'])
    H.append(table(['mode', 'B', 'cost(A)', 'P(1..16)', 'D', 'A forms with this curve / A forms of that cost'], rows, num_cols=(2, 4)))

    # ---- odd curves
    H.append('<h2 id="odd">Curves that are not clean ramps</h2>')
    odd_int = [r for r in R.values() if r['odd'] and r['integral']]
    odd_non = [r for r in R.values() if not r['integral']]
    H.append(f'<p>{len(odd_int)} integral curves are not of the form max(0, c − n) from n = 1 on; {len(odd_non)} curves '
             'are not integral (bus-clock jitter of external-memory accesses).</p>')
    H.append('<h3>Integral, not a ramp: grouped by shape</h3>')
    grp = collections.defaultdict(list)
    for r in odd_int:
        grp[(tuple(r['P'].get(n) for n in NS), r['var'], r['mode'])].append(r)
    rows = []
    for (Pt, var, md), lst in sorted(grp.items(), key=lambda kv: (-len(kv[1]), kv[0][2], kv[0][1])):
        As, Bs = sorted({r['a'] for r in lst}), sorted({r['b'] for r in lst})
        P = {n: v for n, v in zip(NS, Pt) if v is not None}
        if len(Bs) <= len(As):
            who = '<br>'.join(f'<b>{flink(b)}</b> ← ' + ', '.join(flink(r["a"]) for r in sorted(lst, key=lambda r: r['a']) if r['b'] == b) for b in Bs)
        else:
            who = '<br>'.join(f'<b>{flink(a)}</b> → ' + ', '.join(flink(r["b"]) for r in sorted(lst, key=lambda r: r['b']) if r['a'] == a) for a in As)
        rows.append([md, esc(var), f'<span class="c">{pstr(P)}</span>', len(lst), who])
    H.append(table(['mode', 'variant', 'P(0..16)', '#', 'pairs'], rows, num_cols=(3,)))
    H.append('<details><summary>Non-integral curves (%d)</summary>' % len(odd_non))
    rows = [[r['mode'], flink(r['a']), flink(r['b']), esc(r['var']), f'<span class="c">{pstr(r["P"])}</span>', fmt(r['T40'])]
            for r in sorted(odd_non, key=lambda r: (r['a'], r['b'], r['mode'], r['var']))]
    H.append(table(['mode', 'A', 'B', 'variant', 'P(0..16)', 'T(40)'], rows, num_cols=(5,)) + '</details>')

    # ---- freeze
    H.append('<h2 id="freeze">Freeze attribution</h2>')
    H.append('<p>For every latency row: at the first stalled distance n, the extra register-freeze and FPU-freeze cycles '
             'per iteration against n = 40, next to P(n). Summarised by producer kind and consumer kind (M0).</p>')
    agg = collections.defaultdict(lambda: collections.Counter())
    for key, r in R.items():
        if r['kind'] in ('latency', 'implicit') and r['fz'] and r['mode'] == 'M0':
            agg[(category(r['a']), category(r['b']))][freeze_label(r['fz'])] += 1
    rows = [[esc(ka), esc(kb), sum(c.values()), ', '.join(f'{k} {v}' for k, v in c.most_common())] for (ka, kb), c in sorted(agg.items())]
    H.append(table(['producer kind', 'consumer kind', 'latency rows', 'wait counted as'], rows, num_cols=(2,)))
    H.append('<p class="small">"none": the stall shows in the cycle count but in neither freeze counter (issue-slot '
             'or resource conflicts). Per-case values are on each form\'s page.</p>')

    # ---- skipped
    H.append('<h2 id="skipped">Variants not generated</h2>')
    H.append('<p>A variant is dropped when no register assignment and initial values make it safe (an exception, a '
             'pointer outside the scratch, a branch out of the loop, a delay slot it cannot occupy). First reason per '
             'variant, numbers replaced by #.</p>')
    H.append(table(['reason', 'variants'], [[esc(w), k] for w, k in skip_why.most_common()], num_cols=(1,)))

    with open(os.path.join(OUT, 'index.html'), 'w') as f:
        f.write(page('SH7091 pipeline timing sweep', '\n'.join(H)))

    # =========================================================================================== per form
    as_a = collections.defaultdict(list)
    as_b = collections.defaultdict(list)
    for key, r in R.items():
        as_a[r['a']].append(r)
        as_b[r['b']].append(r)
    order = {'latency': 0, 'implicit': 1, 'structural': 2, 'intrinsic': 3, 'masked': 4, 'noisy': 5, 'flat': 6}
    for a in all_forms:
        f = isa.BYNAME[a]
        H = [f'<p class="nav"><a href="../index.html">← index</a></p><h1>{esc(a)}</h1>']
        ops = ', '.join(f'{o.name} ({ {"r": "read", "w": "written", "rw": "read and written"}[o.acc]})' for o in f.ops)
        mems = '; '.join(f'{MEMKIND.get(m.kind, m.kind)} {m.size} B via {m.mode} {m.base}' for m in f.mems)
        H.append(f'<p><span class="mono">{f.enc}</span> · kind: {esc(category(a))} · modes: {", ".join(f.modes)}'
                 + (f' · operands: {esc(ops)}' if ops else '') + (f' · memory: {esc(mems)}' if mems else '')
                 + (' · branch' + (' (delayed)' if f.delayed else '') if f.br else '') + '</p>')
        rows = []
        for md in MODES:
            if (a, md) not in cost:
                continue
            r0 = R[(a, 'NOP', md, 'ind')]
            s = summ.get((a, md))
            rows.append([md, fmt(cost[(a, md)]), fmt(par_delta[(a, md)]), fmt(r0['T40']),
                         (cname[cls_of[a]] + (' (not determined)' if a in ambiguous else '')) if md == 'M0' and a in cls_of else '',
                         s['ncase'] if s else 0])
        H.append(table(['mode', 'cost', 'Δpar', 'T(40) of (A, NOP)', 'issue class', 'cases as A'], rows, num_cols=(1, 2, 3, 5)))
        # pairing detail
        if a in cls_of or any(k[0] == a or k[1] == a for k in pairs):
            rows = []
            for j, g in enumerate(cls):
                fy = [y for y in g if pairs.get((a, y, 'M0')) is True]
                fn = [y for y in g if pairs.get((a, y, 'M0')) is False]
                sy = [y for y in g if pairs.get((y, a, 'M0')) is True]
                sn = [y for y in g if pairs.get((y, a, 'M0')) is False]
                if not (fy or fn or sy or sn):
                    continue
                def cell(yes, no):
                    if yes and not no:
                        return f'<span class="y">Y</span> <span class="small">{len(yes)}</span>'
                    if no and not yes:
                        return f'<span class="no">–</span> <span class="small">{len(no)}</span>'
                    return (f'<span class="small">Y {len(yes)}: {esc(", ".join(yes))}<br>– {len(no)}: {esc(", ".join(no))}</span>')
                rows.append([cname[j], cell(fy, fn), cell(sy, sn)])
            H.append('<h2>Dual issue (bracket pass, M0)</h2>')
            H.append(table(['with class', 'A first, class member second', 'class member first, A second'], rows))
            bb = [(b, q) for (x, b, md), (q, _) in sorted(bad.items()) if x == a]
            if bb:
                H.append('<p class="small">Q outside {1, 2} as first: ' + ', '.join(f'{esc(b)} Q = {fmt(q)}' for b, q in bb) + '</p>')

        def case_rows(lst, producer):
            rows, cls_rows = [], []
            for r in sorted(lst, key=lambda r: (order[r['kind']], r['mode'], r['var'] == 'ind', r['b'] if producer else r['a'], r['var'])):
                other = r['b'] if producer else r['a']
                lab = r.get('imp', '') if r['var'] == 'ind' else dep_label(r['a'], r['b'], r['var'])
                fz = r['fz']
                fzs = f'n={fz[0]}: P {fmt(fz[1])} reg {fmt(fz[2])} fpu {fmt(fz[3])}' if fz else ''
                rows.append([r['mode'], flink(other, '../'), esc(r['var']), esc(lab), r['kind'], fmt(r['D']),
                             fmt(r.get('Dind')) if r['var'] != 'ind' else '', f'<span class="c">{pstr(r["P"])}</span>', fzs,
                             '' if r['ramp'] else 'not a ramp', '' if r['integral'] else 'non-integral'])
                cls_rows.append('odd' if r['odd'] else '')
            return rows, cls_rows
        H.append(f'<h2>As first instruction: every case ({len(as_a[a])})</h2>')
        H.append('<p class="small">kind: latency = waits longer than the independent pair (D is the latency that consumer '
                 'sees); implicit = the independent pair waits on an implicit register A writes and B reads; structural = '
                 'the independent pair waits with no data dependence; intrinsic = B waits like this after almost every A '
                 '(a consumer-side hold); masked = the dependency waits no longer than the independent pair; noisy = the '
                 'curve is not integral (external-memory jitter); flat = nothing binds B at n ≥ 1. D(ind) = D of the '
                 'independent variant. Freeze: extra '
                 'register / FPU freeze cycles at the first stalled n.</p>')
        main_rows = [r for r in as_a[a] if r['kind'] != 'flat' or r['odd']]
        flat_rows = [r for r in as_a[a] if r['kind'] == 'flat' and not r['odd']]
        H.append(f'<input class="f" placeholder="filter" oninput="flt(\'ca\', this.value)">')
        rows, cr = case_rows(main_rows, True)
        hdr = ['mode', 'B', 'variant', 'shares', 'kind', 'D', 'D(ind)', 'P(0..16)', 'freeze at first stall', '', '']
        H.append(table(hdr, rows, cr, tid='ca', num_cols=(5, 6)))
        rows, cr = case_rows(flat_rows, True)
        H.append(f'<details><summary>Flat cases as first instruction ({len(flat_rows)}): B never waits at n ≥ 1</summary>'
                 + table(hdr, rows, cr, num_cols=(5, 6)) + '</details>')
        bl = [r for r in as_b[a] if r['kind'] != 'flat' or r['odd']]
        H.append(f'<h2>As second instruction: cases that wait ({len(bl)} of {len(as_b[a])})</h2>')
        H.append(f'<input class="f" placeholder="filter" oninput="flt(\'cb\', this.value)">')
        rows, cr = case_rows(bl, False)
        H.append(table(['mode', 'A', 'variant', 'shares', 'kind', 'D', 'D(ind)', 'P(0..16)', 'freeze at first stall', '', ''],
                       rows, cr, tid='cb', num_cols=(5, 6)))
        with open(os.path.join(OUT, 'forms', slugs[a] + '.html'), 'w') as fh:
            fh.write(page(a, '\n'.join(H)))
    print(f'{OUT}/index.html + {len(all_forms)} form pages  ({time.time() - t0:.0f} s)')


if __name__ == '__main__':
    main()
