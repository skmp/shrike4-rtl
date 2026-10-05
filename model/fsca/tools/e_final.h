// e_final.h -- DIRECTION 1 infrastructure: the final square product is a SET OF ASSUMPTIONS, not a fact.
//   T2 = ALIGN( ARR(opA, opB) + K3 , u_n )
// with the sine field's width/rounding/normalisation, the operand roles, the rounding constant, the alignment
// rule and the sign/complement encoding all free.  Every variant REGENERATES the latch dataset, so the
// t=17 / t=342 certificates and the "free J -> 0" bound must be recomputed for each one.
// Requirement: u_n must follow ONE stated rule across all nodes -- no per-node fitting.
#pragma once
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>

struct Fin {                       // a final-multiply variant
    int sv;                        // sine field variant
    int r3;                        // 0: arr(J,S)   1: arr(S,J)
    int k3;                        // rounding constant: 0 -> 0, 1 -> 2^19, 2 -> 2^(u-1)
    int sg;                        // 0: positive product, P>>u ; 1: negative product, magnitude by one's comp
};
static const char* SVNAME[9] = {
 "RN23 normalised, u=30+k (current)", "trunc23 normalised, u=30+k", "RN24 normalised, u=31+k",
 "trunc24 normalised, u=31+k", "RN25 normalised, u=32+k", "fixed-point RN(sin*2^24), u=31 (no octave)",
 "fixed-point RN(sin*2^23), u=30 (no octave)", "the 26-bit C0 as the operand, u=33 (no octave)",
 "RN22 normalised, u=29+k" };
static const int NSV = 9;

// sine field and alignment for a variant -- ONE rule across all nodes
static inline void fin_field(int sv,int n,i64&S,int&u){
    int k = TB[n].u2 - 30;                       // the sine's octave (0 for sin in [1/2,1))
    i128 s = sin_(PI_()*n/32);
    auto rn=[&](int sh){ return rn_scale(s,sh); };
    auto tr=[&](int sh){ return (i64)((s<<sh)/QONE); };
    switch(sv){
      case 0: S=std::min(rn(23+k),(i64)((1ll<<23)-1)); u=30+k; break;
      case 1: S=std::min(tr(23+k),(i64)((1ll<<23)-1)); u=30+k; break;
      case 2: S=std::min(rn(24+k),(i64)((1ll<<24)-1)); u=31+k; break;
      case 3: S=std::min(tr(24+k),(i64)((1ll<<24)-1)); u=31+k; break;
      case 4: S=std::min(rn(25+k),(i64)((1ll<<25)-1)); u=32+k; break;
      case 5: S=rn(24); u=31; break;
      case 6: S=rn(23); u=30; break;
      case 7: S=std::min(rn(26),(i64)((1ll<<26)-1)); u=33; break;
      default:S=std::min(rn(22+k),(i64)((1ll<<22)-1)); u=29+k; break; }
    if(n==0) S=0;
}
// the square term for a variant, given the latch J (J >= 0) and the node
static inline i64 fin_T2(const Fin&f,i64 J,i64 S,int u,int du){
    if(!S) return 0; int uu=u+du; if(uu<1||uu>60) return -(1ll<<50);
    i64 K3 = f.k3==0?0 : (f.k3==1?(1ll<<19):(1ll<<(uu-1)));
    i64 P = f.r3? arr(S,J) : arr(J,S);
    if(f.sg==0) return (P+K3)>>uu;                       // positive product
    { i64 Q=-(P+K3); return -(-((~Q)>>uu)); }            // negative product, magnitude by one's complement
}
// monotone in J (checked by the caller), so the window can be inverted by binary search
struct FinCtx { i64 S[17]; int u[17]; i64 JL[1024],JH[1024]; int feasible; int du; };
static Fsca* FIN_F;
static const i64 FIN_C0[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void fin_win(int a,i64&lo,i64&hi){ Win w=decode(FIN_F->srz[a],FIN_F->srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;} }
// build the latch dataset for a variant at latch-scale offset du (0 = coarse, 2 = fine)
static void fin_build(const Fin&f,FinCtx&C,int du){
    C.du=du; for(int n=0;n<=16;n++) fin_field(f.sv,n,C.S[n],C.u[n]);
    i64 LIM=12000000ll<<du; C.feasible=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=LIM;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!C.S[n])continue;
            i64 s=flip?-1:1;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=FIN_C0[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; fin_win(a,lo,hi);
            i64 t2lo=base-hi,t2hi=base-lo;
            i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(fin_T2(f,m,C.S[n],C.u[n],du)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=(a2+b2+1)/2; if(fin_T2(f,m,C.S[n],C.u[n],du)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        C.JL[t+512]=gl; C.JH[t+512]=gh; if(gl<=gh) C.feasible++; }
}
// the free-latch floor: minimum window violations over nodes 0..31 with a free J per t
static long fin_freefloor(const Fin&f,FinCtx&C){
    long tot=0;
    for(int t=-512;t<=511;t++){ if(C.JL[t+512]<=C.JH[t+512]) continue;
        i64 dx=(i64)t<<13; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; int bv=1<<30;
        for(i64 J=std::max((i64)0,C.JL[t+512]-300);J<=C.JH[t+512]+300;J++){ int v=0;
            for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!C.S[n])continue;
                i64 s=flip?-1:1; i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
                i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
                i64 T2=fin_T2(f,J,C.S[n],C.u[n],C.du); i64 V=FIN_C0[n]+s*T1-T2-s*T3;
                int a=(node*1024+t)&0xffff; i64 lo,hi; fin_win(a,lo,hi); if(V<lo||V>hi)v++; }
            if(v<bv)bv=v; }
        tot+=bv; }
    return tot;
}
// guard: nodes 1, 2, 16 and their mirrors, with the published chain
static int fin_guard(const Fin&f,FinCtx&C,int s1,int s2,i64 K1,i64 K2,int ro){
    int g=0; const int gn[5]={1,2,16,30,31};
    for(int i=0;i<5;i++){ int node=gn[i]; int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 I1=((ro?arr(dx,KC):arr(KC,dx))+K1)>>s1; i64 J=(arr(I1,dx)+K2)>>s2;
            i64 T2=fin_T2(f,J,C.S[n],C.u[n],C.du); i64 V=FIN_C0[n]+s*T1-T2-s*T3;
            int a=(node*1024+t)&0xffff; i64 lo,hi; fin_win(a,lo,hi); if(V<lo||V>hi)g++; } }
    return g;
}
