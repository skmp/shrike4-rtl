#!/usr/bin/env python3
"""g6_repair.py -- the constructive half of direction 3.  The exclusions say what the square path is NOT.
This asks the opposite: what is the SMALLEST extra degree of freedom that makes the pinned offsets satisfiable?
Each repair adds ONE (or two) symbolic constants to the chain; the solver then either finds values or proves
none exist.  Whatever the minimal repair turns out to be is a direct description of what the hardware has that
the model lacks."""
import sys, time
sys.path.insert(0,'/tmp/z3env/lib/python3.12/site-packages')
from z3 import *
from g1_arrenc import arr_sym, W
from g4_ladder import arr_ss
from g3_full import arr_cm, bv, JLf, JHf

PINNED = [-376,-303,-40,-17,17,40,258,342,376]
SETS = [("{17,342}", [17,342]), ("{17,342,-342}", [17,342,-342]),
        ("all 9 pinned", PINNED)]

def build(rep, C, K1, K2, X1, X2, s1, s2, t, role1):
    dx = t << 13
    P1 = arr_sym(C,dx) if role1==0 else arr_cm(dx,C)
    I1 = (P1+K1)>>s1
    if rep == 'I1+A': I1 = I1 + X1
    if rep in ('dx+E (both)','dx+E (2nd only)','dx+E per sign'):
        E = X1 if rep != 'dx+E per sign' else (X1 if t>=0 else X2)
        if rep == 'dx+E (both)':
            P1 = arr_ss(C, bv(dx)+E); I1 = (P1+K1)>>s1
        P2 = arr_ss(I1, bv(dx)+E)
    else:
        P2 = arr_sym(I1, dx)
    J = (P2+K2)>>s2
    if rep == 'J+A': J = J + X1
    if rep == 'J+A per sign': J = J + (X1 if t>=0 else X2)
    return J

REPAIRS = ['none','J+A','J+A per sign','I1+A','dx+E (2nd only)','dx+E per sign']

def solve(rep, offs, tmo=40000):
    for role1 in (0,1):
        for s1 in range(19,28):
            s2 = 41-s1
            if s2 < 8 or s2 > 30: continue
            C,K1,K2,X1,X2 = (BitVec(n,W) for n in ('C','K1','K2','X1','X2'))
            s=Solver(); s.set('timeout',tmo)
            # keep repairs SMALL: an unbounded correction would trivially succeed and teach nothing
            for X in (X1,X2):
                s.add(X >= bv(-64), X <= bv(64))
            for t in offs:
                J = build(rep,C,K1,K2,X1,X2,s1,s2,t,role1)
                s.add(J>=bv(JLf[t]), J<=bv(JHf[t]))
            if s.check()==sat:
                m=s.model()
                def v(x):
                    if m[x] is None: return 0
                    y=m[x].as_long(); return y-(1<<W) if y>=(1<<(W-1)) else y
                return True,(role1,s1,v(C),v(K1),v(K2),v(X1),v(X2))
    return False,None

if __name__=='__main__':
    print("Minimal repair: one extra constant (bounded to +-64 so the answer is informative, not trivial)\n")
    print(f"{'repair':<20}" + "".join(f"{n:>18}" for n,_ in SETS)); print("-"*76)
    for rep in REPAIRS:
        row=f"{rep:<20}"
        detail=None
        for name,offs in SETS:
            t0=time.time(); ok,mdl=solve(rep,offs); dt=time.time()-t0
            row += f"{('SAT' if ok else 'unsat')+f' [{dt:.0f}s]':>18}"
            if ok and name=='all 9 pinned': detail=mdl
        print(row)
        if detail:
            print(f"    -> role={detail[0]} s1={detail[1]} C={detail[2]} (KC{detail[2]-0x4ef145:+d}) "
                  f"K1={detail[3]} K2={detail[4]} X1={detail[5]} X2={detail[6]}")
