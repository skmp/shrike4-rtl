#!/usr/bin/env python3
"""g5_pairs.py -- map the impossibility landscape.  For every PAIR of tightly pinned offsets, ask the solver
whether ANY two-multiply chain (any constants, any shift split, either operand role) can satisfy both.
A pair that is UNSAT everywhere is a two-element impossibility certificate, proved over the whole constant
space -- the cheapest possible falsifier for any future candidate of that shape."""
import sys, time, itertools
sys.path.insert(0,'/tmp/z3env/lib/python3.12/site-packages')
from z3 import *
from g1_arrenc import arr_sym, W
from g3_full import arr_cm, bv, JLf, JHf

PINNED = [-376,-303,-40,-17,17,40,258,342,376]

def sat_pair(offs, tmo=20000):
    """True if SOME (role, s1) admits constants satisfying every offset in offs."""
    for role1 in (0,1):
        for s1 in range(19,28):
            s2 = 41-s1
            if s2 < 8 or s2 > 30: continue
            C,K1,K2 = BitVec('C',W),BitVec('K1',W),BitVec('K2',W)
            s = Solver(); s.set('timeout',tmo)
            for t in offs:
                dx = t<<13
                P1 = arr_sym(C,dx) if role1==0 else arr_cm(dx,C)
                I1 = (P1+K1)>>s1
                J = (arr_sym(I1,dx)+K2)>>s2
                s.add(J>=bv(JLf[t]), J<=bv(JHf[t]))
            if s.check()==sat: return True, (role1,s1)
    return False, None

if __name__=='__main__':
    print("Pairs of pinned offsets that NO two-multiply chain can satisfy together")
    print("(any constants, any shift split, either operand role; '.' = satisfiable, X = proved impossible)\n")
    hdr = "        " + "".join(f"{t:>6}" for t in PINNED); print(hdr)
    impossible = []
    for a in PINNED:
        row = f"{a:>7} "
        for b in PINNED:
            if b <= a: row += "     ."; continue
            ok,_ = sat_pair([a,b])
            row += "     X" if not ok else "     ."
            if not ok: impossible.append((a,b))
        print(row)
    print(f"\nproved-impossible pairs: {len(impossible)}")
    for p in impossible: print(f"   {{t = {p[0]}, t = {p[1]}}}")
    # singletons, for reference: which offsets are individually unreachable?
    print("\nindividually unreachable offsets (no chain at all hits them):")
    for t in PINNED:
        ok,_ = sat_pair([t])
        if not ok: print(f"   t = {t}")
    print("   (none listed = every pinned offset is reachable on its own; the impossibility is joint)")
