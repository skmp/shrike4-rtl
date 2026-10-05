#!/usr/bin/env python3
"""g2_solve.py -- DIRECTION 3: let a bit-vector solver choose the hardware constants, rather than scanning them.
For a fixed structure (shift pair, operand roles) ask z3 whether ANY (C, K1, K2) makes the chain hit the pinned
offsets. UNSAT is a PROOF over the whole constant space, which no scan can give."""
import sys, time
sys.path.insert(0, '/tmp/z3env/lib/python3.12/site-packages')
from z3 import *
from g1_arrenc import arr_sym, W

KC = 0x4ef145
JL = {}; JH = {}
for line in open('jwin18.txt'):
    if line.startswith('#'): continue
    p = line.split()
    if len(p) >= 3: JL[int(p[0])] = int(p[1]); JH[int(p[0])] = int(p[2])

def bv(x): return BitVecVal(x & ((1 << W) - 1), W)

def chain(C, K1, K2, s1, s2, t, role1):
    dx = t << 13
    P1 = arr_sym(C, dx) if role1 == 0 else None
    if role1 == 1:
        # arr(dx, KC-like): the recoded operand is dx (a constant), the multiplicand is C (symbolic).
        # Not expressible with arr_sym (which needs a constant multiplicand), so role 1 is handled by g3.
        raise NotImplementedError
    I1 = (P1 + K1) >> s1
    P2 = arr_sym(I1, dx)
    return (P2 + K2) >> s2

def query(s1, s2, offsets, bounded=True, timeout_ms=60000):
    s2_ = s2
    C, K1, K2 = BitVec('C', W), BitVec('K1', W), BitVec('K2', W)
    s = Solver(); s.set('timeout', timeout_ms)
    if bounded:
        s.add(UGE(C, bv(1 << 21)), ULT(C, bv(1 << 24)))
        s.add(UGE(K1, bv(0)), ULT(K1, bv(1 << s1)))
        s.add(UGE(K2, bv(0)), ULT(K2, bv(1 << s2_)))
    for t in offsets:
        J = chain(C, K1, K2, s1, s2_, t, 0)
        s.add(J >= bv(JL[t]), J <= bv(JH[t]))
    r = s.check()
    if r == sat:
        m = s.model()
        def val(v):
            x = m[v].as_long()
            return x - (1 << W) if x >= (1 << (W-1)) else x
        return 'sat', (val(C), val(K1), val(K2))
    return ('unsat' if r == unsat else 'unknown'), None

if __name__ == '__main__':
    print("Does ANY (C, K1, K2) make the chain hit the pinned offsets?  UNSAT = proof over all constants.")
    print("(fine latch scale, s1 + s2 = 41, first multiply arr(C, dx))\n")
    sets = [("t=342 alone", [342]),
            ("t=17 alone", [17]),
            ("{17, 342}", [17, 342]),
            ("{17, 342, -342}", [17, -342, 342])]
    for name, offs in sets:
        print(f"  {name}:")
        for s1 in range(20, 27):
            s2 = 41 - s1
            t0 = time.time()
            r, mdl = query(s1, s2, offs)
            dt = time.time() - t0
            extra = ''
            if r == 'sat': extra = f"  C={mdl[0]} (KC{mdl[0]-KC:+d})  K1={mdl[1]}  K2={mdl[2]}"
            print(f"    s1={s1} s2={s2}: {r:<7} [{dt:5.1f}s]{extra}")
        print()
