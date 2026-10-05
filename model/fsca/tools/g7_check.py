#!/usr/bin/env python3
"""g7_check.py -- SANITY CHECK the solver results before believing them.
g2 reported {17,342} UNSAT for every shift with BOUNDED constants; g5 reported it SAT with UNBOUNDED ones.
Both cannot be reported.  Find out which bound does the work, and whether the two-ladder UNSAT is real or
an artefact of a wrong scale law (a wrong scale makes everything trivially unsatisfiable)."""
import sys, time
sys.path.insert(0,'/tmp/z3env/lib/python3.12/site-packages')
from z3 import *
from g1_arrenc import arr_sym, W
from g4_ladder import arr_ss
from g3_full import arr_cm, bv, JLf, JHf
KC=0x4ef145

def chain_q(offs, s1, role1, bounds, tmo=30000):
    s2=41-s1
    C,K1,K2=BitVec('C',W),BitVec('K1',W),BitVec('K2',W)
    s=Solver(); s.set('timeout',tmo)
    if 'C' in bounds: s.add(UGE(C,bv(1<<21)), ULT(C,bv(1<<24)))
    if 'K' in bounds: s.add(UGE(K1,bv(0)),ULT(K1,bv(1<<s1)),UGE(K2,bv(0)),ULT(K2,bv(1<<s2)))
    for t in offs:
        dx=t<<13
        P1=arr_sym(C,dx) if role1==0 else arr_cm(dx,C)
        I1=(P1+K1)>>s1
        J=(arr_sym(I1,dx)+K2)>>s2
        s.add(J>=bv(JLf[t]), J<=bv(JHf[t]))
    r=s.check()
    if r==sat:
        m=s.model()
        def v(x):
            y=m[x].as_long(); return y-(1<<W) if y>=(1<<(W-1)) else y
        return 'sat',(v(C),v(K1),v(K2))
    return ('unsat' if r==unsat else '?'),None

print("(1) WHICH BOUND does the work for {17,342}?  role 0, per shift:")
for s1 in range(19,28):
    row=f"   s1={s1:2d} s2={41-s1:2d}: "
    for name,b in (("bounded C+K",'CK'),("bounded C only",'C'),("bounded K only",'K'),("unbounded","")):
        r,mdl=chain_q([17,342],s1,0,b)
        row += f"{name}={r}"
        if r=='sat' and name=="unbounded": row += f" (C={mdl[0]} K1={mdl[1]} K2={mdl[2]})"
        row += "  "
    print(row)
print()
print("(2) both roles, unbounded, {17,342}:")
for role1 in (0,1):
    row=f"   role {role1}: "
    for s1 in range(19,28):
        r,mdl=chain_q([17,342],s1,role1,"")
        row+=f"s1={s1}:{r} "
    print(row)
print()
print("(3) TWO-LADDER sanity: is it satisfiable for a SINGLE offset?  (if not, the scale law is wrong)")
for s3 in (10,11,12):
    for s1 in (21,23):
        s2=54-s3-s1
        C1,C2,K1,K2,K3=(BitVec(n,W) for n in ('C1','C2','K1','K2','K3'))
        s=Solver(); s.set('timeout',60000)
        t=342; dx=t<<13
        L1=(arr_sym(C1,dx)+K1)>>s1; L2=(arr_sym(C2,dx)+K2)>>s2
        J=(arr_ss(L1,L2)+K3)>>s3
        s.add(J>=bv(JLf[t]), J<=bv(JHf[t]))
        r=s.check()
        print(f"   s1={s1} s2={s2} s3={s3}, t=342 alone: {'sat' if r==sat else ('unsat' if r==unsat else '?')}")
