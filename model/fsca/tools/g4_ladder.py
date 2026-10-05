#!/usr/bin/env python3
"""g4_ladder.py -- a fully symbolic array (BOTH operands symbolic), so the TWO-LADDER family can be put to the
solver too:  L1 = (arr(C1,dx)+K1)>>s1 ; L2 = (arr(C2,dx)+K2)>>s2 ; J = (arr(L1,L2)+K3)>>s3.
My earlier scan tried 1.8e10 configurations of this family and none passed t=17; a solver UNSAT turns that into
a proof over all four constants at once."""
import sys, time
sys.path.insert(0, '/tmp/z3env/lib/python3.12/site-packages')
from z3 import *
from g1_arrenc import arr_sym, arr_const, W

def arr_ss(rec, mul, w=W):
    """arr(rec, mul) with BOTH symbolic."""
    T = BitVecVal(0, w); R = []
    mul2 = mul << 1
    for k in range(14):
        def bitof(b):
            if b < 0: return BitVecVal(0,1)
            return Extract(min(b,w-1), min(b,w-1), rec)
        sel = Concat(bitof(2*k+1), bitof(2*k), bitof(2*k-1))
        rows = []; negs = []
        for s in range(8):
            v_bm1 = s & 1; v_b0 = (s>>1)&1; v_b1 = (s>>2)&1
            d = -2*v_b1 + v_b0 + v_bm1
            if d == 0: rows.append(BitVecVal(0,w)); negs.append(False)
            elif abs(d) == 1: rows.append((~mul) if d<0 else mul); negs.append(d<0)
            else: rows.append((~mul2) if d<0 else mul2); negs.append(d<0)
        row = rows[7]
        for s in range(6,-1,-1): row = If(sel == BitVecVal(s,3), rows[s], row)
        hot = BitVecVal(0,w)
        if 2*k >= 18:
            h = BitVecVal(1 << (2*k), w)
            e = h if negs[7] else BitVecVal(0,w)
            for s in range(6,-1,-1): e = If(sel == BitVecVal(s,3), (h if negs[s] else BitVecVal(0,w)), e)
            hot = e
        Rk = row << (2*k)
        T = T + ((Rk >> 16) << 16) + hot
        R.append(Rk)
    m15 = ZeroExt(w-1,Extract(15,15,R[0])) + ZeroExt(w-1,Extract(15,15,R[1])) + ZeroExt(w-1,Extract(15,15,R[2]))
    T = T + If(UGE(m15, BitVecVal(2,w)), BitVecVal(1<<16,w), BitVecVal(0,w))
    p16 = Extract(16,16,R[6]) ^ Extract(16,16,R[7]) ^ Extract(16,16,R[8])
    T = T - If(p16 == BitVecVal(1,1), BitVecVal(1<<16,w), BitVecVal(0,w))
    return T

def bv(x): return BitVecVal(x & ((1<<W)-1), W)
JL={}; JH={}
for line in open('jwin18.txt'):
    if line.startswith('#'): continue
    p=line.split()
    if len(p)>=3: JL[int(p[0])]=int(p[1]); JH[int(p[0])]=int(p[2])

if __name__ == '__main__':
    import random
    random.seed(11)
    bad=0
    for _ in range(120):
        rv=random.randint(-(1<<21),1<<22); mv=random.randint(-(1<<21),1<<22)
        e=simplify(arr_ss(bv(rv), bv(mv)))
        v=e.as_long()
        if v>=(1<<(W-1)): v-=(1<<W)
        if v != arr_const(rv,mv): bad+=1
    print(f"fully symbolic array encoder vs the C++ model: {bad} mismatches of 120\n")
    OFF=[17,342]
    print("TWO-LADDER family, solver.  L1=(arr(C1,dx)+K1)>>s1 ; L2=(arr(C2,dx)+K2)>>s2 ; J=(arr(L1,L2)+K3)>>s3")
    print("Is there ANY (C1,C2,K1,K2,K3) hitting both t=17 and t=342?   (s1+s2+s3 = 41 + 13 for the extra dx factor)")
    for s3 in range(9, 15):
        line=f"  s3={s3}: "
        for s1 in range(20, 26):
            s2 = 54 - s3 - s1        # scale law: alpha*beta = 157.88*2^s3 with both latches ~ C*t*2^13/2^s
            if s2 < 12 or s2 > 32: continue
            C1,C2,K1,K2,K3 = BitVec('C1',W),BitVec('C2',W),BitVec('K1',W),BitVec('K2',W),BitVec('K3',W)
            s=Solver(); s.set('timeout', 90000)
            for t in OFF:
                dx=t<<13
                L1=(arr_sym(C1,dx)+K1)>>s1
                L2=(arr_sym(C2,dx)+K2)>>s2
                J=(arr_ss(L1,L2)+K3)>>s3
                s.add(J>=bv(JL[t]), J<=bv(JH[t]))
            t0=time.time(); r=s.check(); dt=time.time()-t0
            line += f"s1={s1}:{'sat' if r==sat else ('unsat' if r==unsat else '?')}[{dt:.0f}s] "
        print(line)
