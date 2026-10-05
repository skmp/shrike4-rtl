#!/usr/bin/env python3
"""g1_arrenc.py -- DIRECTION 3 step 1: encode the FIPR Booth array as bit-vector operations, symbolically in
the RECODED operand, with the multiplicand a known constant (which it is, per offset: mul = dx = t<<13).
Each Booth digit reads 3 consecutive bits of rec, so each row is an 8-way select among CONSTANTS -- the whole
array becomes 14 selects plus adds, which a solver handles easily.
This file only builds and VALIDATES the encoder against the C++ arr(); the search is in g2."""
import sys
sys.path.insert(0, '/tmp/z3env/lib/python3.12/site-packages')
from z3 import *

W = 96   # bit-vector width: wide enough for arr outputs (~2^46) with headroom for negatives

def arr_const(rec_val, mul):
    """reference implementation, mirroring base2.h arr() exactly, in python ints"""
    R = [0]*14; T = 0
    def bit(b):
        if b < 0: return 0
        return (rec_val >> min(b,62)) & 1 if rec_val >= 0 else ((rec_val >> min(b,62)) & 1)
    for k in range(14):
        d = -2*bit(2*k+1) + bit(2*k) + bit(2*k-1)
        mag = abs(d)*mul
        row = ~mag if d < 0 else mag
        R[k] = row * (1 << (2*k))
        T += (R[k] >> 16) << 16
        if d < 0 and 2*k >= 18: T += 1 << (2*k)
    m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1)
    T += (1 << 16) * (1 if m15 >= 2 else 0)
    p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1
    T -= (1 << 16) * p16
    return T

def arr_sym(rec, mul, w=W):
    """arr(rec, mul) with rec a symbolic BitVec(w) and mul a python int constant."""
    def bitof(b):
        if b < 0: return BitVecVal(0, 1)
        return Extract(min(b, w-1), min(b, w-1), rec)
    T = BitVecVal(0, w)
    r15 = []; r16 = []
    for k in range(14):
        bm1, b0, b1 = bitof(2*k-1), bitof(2*k), bitof(2*k+1)
        sel = Concat(b1, b0, bm1)                       # 3-bit selector -> one of 8 cases
        # precompute the 8 constant outcomes for this k
        contrib = []; bit15 = []; bit16 = []
        for s in range(8):
            v_bm1 = s & 1; v_b0 = (s >> 1) & 1; v_b1 = (s >> 2) & 1
            d = -2*v_b1 + v_b0 + v_bm1
            mag = abs(d)*mul
            row = ~mag if d < 0 else mag
            Rk = row * (1 << (2*k))
            c = (Rk >> 16) << 16
            if d < 0 and 2*k >= 18: c += 1 << (2*k)
            contrib.append(c); bit15.append((Rk >> 15) & 1); bit16.append((Rk >> 16) & 1)
        e = BitVecVal(contrib[7] & ((1 << w) - 1), w)
        for s in range(6, -1, -1):
            e = If(sel == BitVecVal(s, 3), BitVecVal(contrib[s] & ((1 << w) - 1), w), e)
        T = T + e
        if k < 3:
            b = BitVecVal(bit15[7], 1)
            for s in range(6, -1, -1): b = If(sel == BitVecVal(s, 3), BitVecVal(bit15[s], 1), b)
            r15.append(b)
        if 6 <= k <= 8:
            b = BitVecVal(bit16[7], 1)
            for s in range(6, -1, -1): b = If(sel == BitVecVal(s, 3), BitVecVal(bit16[s], 1), b)
            r16.append(b)
    m15 = ZeroExt(w-1, r15[0]) + ZeroExt(w-1, r15[1]) + ZeroExt(w-1, r15[2])
    T = T + If(UGE(m15, BitVecVal(2, w)), BitVecVal(1 << 16, w), BitVecVal(0, w))
    p16 = r16[0] ^ r16[1] ^ r16[2]
    T = T - If(p16 == BitVecVal(1, 1), BitVecVal(1 << 16, w), BitVecVal(0, w))
    return T

if __name__ == '__main__':
    import random
    random.seed(7)
    # validate the symbolic encoder against the reference for random recs and the muls FSCA actually uses
    bad = 0; n = 0
    muls = [t << 13 for t in (1, 17, -17, 342, -342, 511, -512, 100, -7)] + [5173573, 8388607]
    for mul in muls:
        for _ in range(60):
            rv = random.randint(-(1 << 22), 1 << 23)
            rec = BitVecVal(rv & ((1 << W) - 1), W)
            e = simplify(arr_sym(rec, mul))
            got = e.as_signed_long() if hasattr(e, 'as_signed_long') else None
            v = e.as_long()
            if v >= (1 << (W-1)): v -= (1 << W)
            ref = arr_const(rv, mul)
            n += 1
            if v != ref:
                bad += 1
                if bad <= 3: print(f"  MISMATCH rec={rv} mul={mul}: sym={v} ref={ref} diff={v-ref}")
    print(f"symbolic array encoder vs the C++ model: {bad} mismatches of {n} random cases")
