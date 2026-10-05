#!/usr/bin/env python3
"""g3_full.py -- DIRECTION 3, the complete exclusion.  Two encoders:
   arr_sym(sym_rec, const_mul)  -- rows are 8-way selects on 3 bits of rec   (g1)
   arr_cm (const_rec, sym_mul)  -- Booth digits are KNOWN, rows are 0/+-mul/+-2mul   (here)
so BOTH operand roles are expressible.  Constants left UNBOUNDED, so UNSAT is a proof over every
constant a designer could have stored."""
import sys, time
sys.path.insert(0, '/tmp/z3env/lib/python3.12/site-packages')
from z3 import *
from g1_arrenc import arr_sym, W, arr_const

KC = 0x4ef145
def load(fn):
    L, H = {}, {}
    for line in open(fn):
        if line.startswith('#'): continue
        p = line.split()
        if len(p) >= 3: L[int(p[0])] = int(p[1]); H[int(p[0])] = int(p[2])
    return L, H
JLf, JHf = load('jwin18.txt')     # fine scale,  J ~ KC t^2 / 2^15, s1+s2 = 41
JLc, JHc = load('jwin.txt')       # coarse scale, J ~ KC t^2 / 2^17, s1+s2 = 43
def bv(x): return BitVecVal(x & ((1 << W) - 1), W)

def arr_cm(rec_val, mul, w=W):
    """arr(rec, mul) with rec a KNOWN int and mul a symbolic BitVec."""
    T = BitVecVal(0, w); R = []
    for k in range(14):
        def bit(b):
            if b < 0: return 0
            return (rec_val >> min(b, 62)) & 1
        d = -2*bit(2*k+1) + bit(2*k) + bit(2*k-1)
        if d == 0:
            mag = BitVecVal(0, w)
        elif abs(d) == 1:
            mag = mul
        else:
            mag = mul << 1
        row = (~mag) if d < 0 else mag
        Rk = row << (2*k)
        T = T + ((Rk >> 16) << 16)
        if d < 0 and 2*k >= 18: T = T + BitVecVal(1 << (2*k), w)
        R.append(Rk)
    m15 = ZeroExt(w-1, Extract(15,15,R[0])) + ZeroExt(w-1, Extract(15,15,R[1])) + ZeroExt(w-1, Extract(15,15,R[2]))
    T = T + If(UGE(m15, BitVecVal(2, w)), BitVecVal(1 << 16, w), BitVecVal(0, w))
    p16 = Extract(16,16,R[6]) ^ Extract(16,16,R[7]) ^ Extract(16,16,R[8])
    T = T - If(p16 == BitVecVal(1,1), BitVecVal(1 << 16, w), BitVecVal(0, w))
    return T

def build(C, K1, K2, s1, s2, t, role1, g=0, K3=None):
    """g > 0 adds a THIRD truncation (J+K3)>>g, the family e5 scanned."""
    dx = t << 13
    P1 = arr_sym(C, dx) if role1 == 0 else arr_cm(dx, C)
    I1 = (P1 + K1) >> s1
    P2 = arr_sym(I1, dx)
    J = (P2 + K2) >> s2
    if g: J = (J + K3) >> g
    return J

def query(s1, s2, offs, role1, JL, JH, g=0, bounded=False, tmo=120000):
    C, K1, K2 = BitVec('C', W), BitVec('K1', W), BitVec('K2', W)
    K3 = BitVec('K3', W)
    s = Solver(); s.set('timeout', tmo)
    if bounded:
        s.add(UGE(C, bv(1 << 21)), ULT(C, bv(1 << 24)))
    for t in offs:
        J = build(C, K1, K2, s1, s2, t, role1, g, K3)
        s.add(J >= bv(JL[t]), J <= bv(JH[t]))
    r = s.check()
    if r == sat:
        m = s.model()
        def val(v):
            if m[v] is None: return 0
            x = m[v].as_long(); return x - (1 << W) if x >= (1 << (W-1)) else x
        return 'sat', (val(C), val(K1), val(K2), val(K3))
    return ('unsat' if r == unsat else '?'), None

if __name__ == '__main__':
    OFF = [17, 342]
    print("CORE EXCLUSION.  Is there ANY (C, K1, K2), unbounded, with the chain hitting BOTH t=17 and t=342?")
    print("  chain: I1 = (arr1 + K1) >> s1 ;  J = (arr(I1,dx) + K2) >> s2\n")
    for tag, (JL, JH, tot) in (("fine  (s1+s2=41)", (JLf, JHf, 41)), ("coarse (s1+s2=43)", (JLc, JHc, 43))):
        print(f"  {tag}")
        for role1 in (0, 1):
            rn = "arr(C,dx)" if role1 == 0 else "arr(dx,C)"
            line = f"    first multiply {rn}: "
            for s1 in range(18, 29):
                s2 = tot - s1
                if s2 < 8 or s2 > 30: continue
                r, mdl = query(s1, s2, OFF, role1, JL, JH)
                line += f"s1={s1}:{r} "
            print(line)
        print()
    print("THIRD LATCH.  Same question with an extra truncation (J+K3)>>g, K3 free:")
    for g in (1, 2, 3, 4):
        line = f"    g={g}: "
        for s1 in range(20, 27):
            s2 = 41 - g - s1
            if s2 < 8: continue
            r, _ = query(s1, s2, OFF, 0, JLf, JHf, g=g)
            line += f"s1={s1}:{r} "
        print(line)
