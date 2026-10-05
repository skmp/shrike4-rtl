// f2_core.h -- exhaustive (NOT binary-searched) decision of "does ANY integer I1 put J in its window?"
//
// e6_nol1.cpp bracketed I1 by binary search, which assumes arr(v,mul) is monotone in v.
// f2_arrprobe.cpp measures that it is NOT: 201492 backward steps over the sampled band, worst -65536.
// So every claim of the form "no integer I1 exists" has to be re-decided by scanning the whole bracket.
//
// The bracket is provable: arr(v,mul) - v*mul is bounded (measured [-448000, 0]; the structural bound
// from 14 truncated rows + the 9 lost hot ones + the two group terms is < 1135957).  We use EB = 1<<21.
#pragma once
#include "e_final.h"

static const i64 EB = 1ll<<21;              // safe bound on |arr(v,mul) - v*mul|
static const i64 VMAX = 1ll<<26;            // the model's Booth recoder covers 14 digits = 28 signed bits

// latch rules on the SUM P = ARR + K2
enum { LR_FLOOR=0, LR_TZ=1, LR_OCM=2 };
static inline i64 latch(i64 P,int s,int rule){
    if(rule==LR_FLOOR) return P>>s;
    if(rule==LR_TZ)    return P>=0 ? (P>>s) : -((-P)>>s);
    return P>=0 ? (P>>s) : -((~P)>>s);
}
// inverse of the latch: the set of P giving J in [JL,JH] is an interval [Plo,Phi] (all three rules are
// monotone non-decreasing in P), found by bisection on P -- P -> latch(P,s,rule) IS monotone, unlike arr.
static inline void latch_inv(i64 JL,i64 JH,int s,int rule,i64&Plo,i64&Phi){
    i64 lo=-(1ll<<57), hi=(1ll<<57);   // NOT 2^62: hi-lo overflows i64 inside the bisection
    { i64 a=lo,b=hi; while(a<b){ i64 m=a+((b-a)>>1); if(latch(m,s,rule)>=JL) b=m; else a=m+1; } Plo=a; }
    { i64 a=lo,b=hi; while(a<b){ i64 m=a+((b-a+1)>>1); if(latch(m,s,rule)<=JH) a=m; else b=m-1; } Phi=a; }
}
struct Cert { int hit; i64 vhit; i64 vlo,vhi; long long scanned; int capped; int clamped; i64 need; };
// role 0: ARR = arr(I1, dx)      role 1: ARR = arr(dx, I1)
static inline i64 ARRof(int role,i64 v,i64 dx){ return role? arr(dx,v) : arr(v,dx); }

// Exhaustively decide t: is there an integer I1 with latch(ARR + K2, s2, rule) in [JL,JH]?
// Scans the ENTIRE provable bracket (centre-out, early exit on the first hit).
static Cert certify(int role,i64 dx,i64 K2,int s2,int rule,i64 JL,i64 JH,long long CAP=4000000ll){
    Cert c; c.hit=0; c.vhit=0; c.scanned=0; c.capped=0; c.clamped=0; c.need=0;
    i64 Plo,Phi; latch_inv(JL,JH,s2,rule,Plo,Phi);
    i64 Alo=Plo-K2, Ahi=Phi-K2;                       // required ARR range
    i64 m=(dx<0?-dx:dx); if(!m){ c.vlo=c.vhi=0; return c; }
    // v*|dx| must lie in [Alo-EB, Ahi+EB] up to sign
    i64 lo,hi;
    i64 L=Alo-EB, H=Ahi+EB;
    auto fdiv=[](i64 a,i64 b){ i64 q=a/b; if((a%b)&&((a<0)!=(b<0))) q--; return q; };
    auto cdiv=[](i64 a,i64 b){ i64 q=a/b; if((a%b)&&((a<0)==(b<0))) q++; return q; };
    if(dx>0){ lo=cdiv(L,dx); hi=fdiv(H,dx); } else { lo=cdiv(H,dx); hi=fdiv(L,dx); }
    c.need = std::max(lo<0?-lo:lo, hi<0?-hi:hi);   // largest |I1| the window asks for
    if(lo<-VMAX){ lo=-VMAX; c.clamped=1; } if(hi>VMAX){ hi=VMAX; c.clamped=1; }
    c.vlo=lo; c.vhi=hi;
    if(lo>hi) return c;
    i64 mid=lo+((hi-lo)>>1);
    for(i64 d=0; ; d++){
        i64 a=mid+d, b=mid-d; int any=0;
        if(a<=hi){ any=1; i64 P=ARRof(role,a,dx)+K2; i64 J=latch(P,s2,rule); c.scanned++;
                   if(J>=JL&&J<=JH){ c.hit=1; c.vhit=a; return c; } }
        if(d&&b>=lo){ any=1; i64 P=ARRof(role,b,dx)+K2; i64 J=latch(P,s2,rule); c.scanned++;
                   if(J>=JL&&J<=JH){ c.hit=1; c.vhit=b; return c; } }
        if(!any) break;
        if(c.scanned>CAP){ c.capped=1; break; }
    }
    return c;
}
