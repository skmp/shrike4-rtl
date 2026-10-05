// f1_final.h -- DIRECTION 1, part A: the final square product's REPRESENTATION, swept exhaustively.
//     T2 = ALIGN_sg( ARR(opA,opB) + K3 , u_n )
// Free: the sine field's WIDTH, its ROUNDING, its NORMALISATION (and its SOURCE -- derived sine or the
// hardware C0 table), the ALIGNMENT rule u_n = a + b*k, the OPERAND ROLES, the ROUNDING CONSTANT and the
// SIGN/COMPLEMENT encoding.  u_n must follow ONE rule across all nodes: only (a,b) are free, never per node.
// Everything variant-independent (C0, T1, T3, the dump windows) is precomputed once.
#pragma once
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cmath>

static Fsca FF;
static const i64 F1_C0[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,
                            51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};

// ---- variant-independent precomputation: the interval the square term must land in, per (t,node)
static i64 T2LO[1024][32], T2HI[1024][32];
static i64 VLO[1024][32], VHI[1024][32];   // the output window itself (for full-model scoring)
static i64 BASE[1024][32];                 // C0 + s*T1 - s*T3
static void f1_precompute(){
    for(int t=-512;t<=511;t++){ int it=t+512; i64 dx=(i64)t<<13;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=F1_C0[n]+s*T1-s*T3;
            int a=(node*1024+t)&0xffff; Win w=decode(FF.srz[a],FF.srn[a]);
            i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}
            VLO[it][node]=lo; VHI[it][node]=hi; BASE[it][node]=base;
            T2LO[it][node]=base-hi; T2HI[it][node]=base-lo; } }
}

// ---- the variant
struct FVar { int W, rm, fm, b, a, role, kc, sg; };
static const char* RMNAME[4]={"RNe","trunc","half-up","ceil"};
static const char* FMNAME[5]={"norm-sine-ROM","fixed-sine-ROM","fixed-ROM+shifter","C0-table-fixed","C0-table-norm"};
static const char* KCNAME[5]={"0","2^19","2^(u-1)","2^16","2^19+2^16"};
static const char* SGNAME[2]={"pos","neg-1c"};

static inline i64 rnd_q60(i128 v,int sh,int rm){          // R(v * 2^sh), v in Q60 >= 0
    i128 num = sh>=0 ? (v<<sh) : (v>>(-sh));
    i128 q=num/QONE, r=num%QONE;
    switch(rm){ case 0: if(r*2>QONE||(r*2==QONE&&(q&1)))++q; break;
                case 1: break;
                case 2: if(r*2>=QONE)++q; break;
                default: if(r)++q; }
    return (i64)q;
}
static inline i64 rnd_int(i64 v,int d,int rm){            // R(v / 2^d), v >= 0, d >= 0
    if(d<=0) return v<<(-d);
    i64 q=v>>d, r=v&((1ll<<d)-1), h=1ll<<(d-1);
    switch(rm){ case 0: if(r>h||(r==h&&(q&1)))++q; break;
                case 1: break;
                case 2: if(r>=h)++q; break;
                default: if(r)++q; }
    return q;
}
// ONE rule across all nodes.  Returns S[1..16] and u[1..16]; kkout[n] = the octave used by the aligner.
static void f1_field(const FVar&v,i64 S[17],int u[17],int kk[17]){
    for(int n=1;n<=16;n++){
        int k = TB[n].u2 - 30;                    // the sine's octave: sin in [2^-(k+1), 2^-k)
        i128 s = sin_(PI_()*n/32);
        i64 f;
        switch(v.fm){
          case 0: f = rnd_q60(s, v.W + k, v.rm); break;                     // normalised sine ROM
          case 1: f = rnd_q60(s, v.W,     v.rm); break;                     // fixed-point sine ROM
          case 2: f = rnd_q60(s, v.W,     v.rm) << k; break;                // fixed ROM + normalising shifter
          case 3: f = rnd_int(F1_C0[n], 26 - v.W, v.rm); break;             // the hardware C0 table, fixed
          default:f = rnd_int(F1_C0[n] << k, 26 - v.W, v.rm); break;        // C0 table, normalised
        }
        i64 cap = (v.fm==1||v.fm==3) ? ((1ll<<(v.W-k))-1) : ((1ll<<v.W)-1);
        if(f>cap) f=cap; if(f<1) f=1;
        S[n]=f; kk[n]=k; u[n]=v.a + v.b*k;
    }
    S[0]=0;u[0]=v.a;kk[0]=0;
}
static inline i64 f1_T2(const FVar&v,i64 J,i64 S,int u){
    if(u<1||u>60) return -(1ll<<52);
    i64 K3; switch(v.kc){ case 0:K3=0;break; case 1:K3=1ll<<19;break; case 2:K3=1ll<<(u-1);break;
                          case 3:K3=1ll<<16;break; default:K3=(1ll<<19)+(1ll<<16); }
    i64 P = v.role ? arr(S,J) : arr(J,S);
    if(v.sg==0) return (P+K3)>>u;
    return (P+K3-1)>>u;            // negative product, magnitude by one's complement: -((~(-(P+K3)))>>u)
}
// ---- build the latch dataset for a variant.  Early abort once `maxbad` offsets are infeasible.
struct Ctx { i64 S[17]; int u[17],kk[17]; i64 JL[1024],JH[1024]; int feasible; bool aborted; };
static const int PROBE[24]={17,-17,342,-342,385,-385,376,-376,258,-258,420,-420,431,-431,303,-303,
                            511,-511,468,-468,377,-377,100,-100};
static bool f1_build(const FVar&v,Ctx&C,int maxbad){
    f1_field(v,C.S,C.u,C.kk); C.feasible=0; C.aborted=false;
    // The Booth recoder in arr() covers 14 digits = bits 0..26, so a recoded operand MUST be < 2^27;
    // above that arr() stops being monotone (measured: first non-monotone J is exactly 2^27).  A variant
    // whose latch would need a wider operand simply comes out infeasible, which is the physical answer.
    const i64 LIM = (1ll<<27)-1;
    int bad=0; bool done[1024]; memset(done,0,sizeof(done));
    auto one=[&](int t)->bool{ int it=t+512; i64 gl=0,gh=LIM;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 Sv=C.S[n]; int uu=C.u[n];
            i64 t2lo=T2LO[it][node], t2hi=T2HI[it][node];
            i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=a1+((b1-a1)>>1); if(f1_T2(v,m,Sv,uu)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=a2+((b2-a2+1)>>1); if(f1_T2(v,m,Sv,uu)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; if(gl>gh) break; }
        C.JL[it]=gl; C.JH[it]=gh; return gl<=gh; };
    for(int i=0;i<24;i++){ int t=PROBE[i]; if(done[t+512])continue; done[t+512]=true;
        if(one(t)){C.feasible++;} else if(++bad>3){C.aborted=true;return false;} }
    for(int t=-512;t<=511;t++){ if(done[t+512])continue; done[t+512]=true;
        if(one(t)){C.feasible++;} else if(++bad>maxbad){C.aborted=true;return false;} }
    return true;
}
// ---- the free-latch floor (only infeasible t contribute; 0 iff every t is feasible)
static long f1_freefloor(const FVar&v,Ctx&C){
    long tot=0;
    for(int t=-512;t<=511;t++){ int it=t+512; if(C.JL[it]<=C.JH[it]) continue;
        int bv=1<<30;
        for(i64 J=std::max((i64)0,C.JL[it]-400);J<=C.JH[it]+400;J++){ int viol=0;
            for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
                i64 T2=f1_T2(v,J,C.S[n],C.u[n]); i64 V=BASE[it][node]-T2;
                if(V<VLO[it][node]||V>VHI[it][node])viol++; }
            if(viol<bv)bv=viol; }
        tot+=bv; }
    return tot;
}
// ---- the chain J = (arr(I1,dx)+K2)>>s2, I1 = (arr(.,.)+K1)>>s1 : latch-window score
struct Chain { int s1,s2,ro; i64 K1,K2; int miss; };
static int f1_chain_score(const Ctx&C,int s1,int s2,i64 K1,i64 K2,int ro){
    int sc=0; for(int t=-512;t<=511;t++){ int it=t+512; if(C.JL[it]>C.JH[it])continue; i64 dx=(i64)t<<13;
        i64 I1=((ro?arr(dx,KC):arr(KC,dx))+K1)>>s1; i64 J=(arr(I1,dx)+K2)>>s2;
        if(J>=C.JL[it]&&J<=C.JH[it])sc++; }
    return 1024-sc;
}
// full-model window violations over nodes 0..31, and the guard nodes separately
static int f1_full(const FVar&v,const Ctx&C,const Chain&ch,int gper[4]){
    int tot=0; for(int i=0;i<4;i++)gper[i]=0;
    for(int t=-512;t<=511;t++){ int it=t+512; i64 dx=(i64)t<<13;
        i64 I1=((ch.ro?arr(dx,KC):arr(KC,dx))+ch.K1)>>ch.s1; i64 J=(arr(I1,dx)+ch.K2)>>ch.s2;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
            i64 T2 = (node==0)?0:f1_T2(v,J,C.S[n],C.u[n]);
            i64 V=BASE[it][node]-T2;
            if(V<VLO[it][node]||V>VHI[it][node]){ tot++;
                if(node==0)gper[0]++; else if(node==1||node==31)gper[1]++;
                else if(node==2||node==30)gper[2]++; else if(node==16)gper[3]++; } } }
    return tot;
}
// ---- does an integer I1 exist putting J(t) inside its window, for a given (s2,K2)?
static inline bool f1_hasI1(const Ctx&C,int t,int s2,i64 K2){
    int it=t+512; if(C.JL[it]>C.JH[it]) return false; if(t==0) return true;
    i64 dx=(i64)t<<13; auto Jof=[&](i64 x){ return (arr(x,dx)+K2)>>s2; };
    bool inc = Jof(1<<14)>Jof(0); const i64 lim=(1ll<<27)-1; i64 a1,b1,a2,b2;   // the recoded operand must stay inside the 14-digit Booth range
    if(inc){ a1=-lim;b1=lim+1; while(a1<b1){i64 m=a1+((b1-a1)>>1); if(Jof(m)>=C.JL[it])b1=m;else a1=m+1;}
             a2=-lim-1;b2=lim;  while(a2<b2){i64 m=a2+((b2-a2+1)>>1); if(Jof(m)<=C.JH[it])a2=m;else b2=m-1;} }
    else   { a1=-lim;b1=lim+1; while(a1<b1){i64 m=a1+((b1-a1)>>1); if(Jof(m)<=C.JH[it])b1=m;else a1=m+1;}
             a2=-lim-1;b2=lim;  while(a2<b2){i64 m=a2+((b2-a2+1)>>1); if(Jof(m)>=C.JL[it])a2=m;else b2=m-1;} }
    return a1<=a2;
}
