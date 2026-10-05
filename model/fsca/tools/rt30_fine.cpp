// rt30_fine.cpp -- rt29 with the intersections rebuilt at an ARBITRARY J resolution (g extra fraction bits: T2 =
// sm(arr(Jg,S)+K, u2+g)), for the real chip and for per-word-error synthetic chips.  The notes escaped the +-17
// contradiction by giving J two extra bits; does per-word error predict that escape too?
// (original header follows)
// rt29_common.cpp -- COMMON J or PER-WORD error?  Decided by the number of offsets at which the words' individual
// J-windows have an EMPTY intersection (real chip: 2 of 1024, t = +-17).
// Synthetic chips, both with errors of the measured size (about -4..+2 coarse J units, rt22):
//   P1  common      : J_hw(t) = J_chain(t) + d(t),          d(t) integer, one draw per offset
//   P2  independent : each ROM word's product uses its own  d_n(t)  (real-valued, one draw per (word, offset))
// Floats are generated (RN and RZ), decoded exactly like the dump, and the coarse windows rebuilt from scratch.
#include "base2.h"
#include <random>
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static i64 BASE[32][1024]; static int TABN[32]; static i64 Jc[1024];
// intersection of the per-node integer-J windows at offset index i, given per-node hardware V windows
static int G=0;
static int count_empty(i64 (*LO)[1024], i64 (*HI)[1024], double* meanw){ int empty=0; double sw=0; int nw=0;
    for(int i=0;i<1024;i++){ if(i==512)continue; i64 gl=-(1ll<<60), gh=(1ll<<60);
        for(int m=1;m<32;m++){ const Tab&T=TB[TABN[m]]; if(!T.S)continue; i64 lo=LO[m][i],hi=HI[m][i];
            // T2 must lie in [base-hi, base-lo];  T2 = (J*S + K - loss) >> u  is monotone in J up to the tiny loss: bracket, then refine exactly
            i64 t2lo=BASE[m][i]-hi, t2hi=BASE[m][i]-lo; int u=T.u2+G; long double unit=(long double)(1ll<<u)/(long double)T.S;
            i64 jl=(i64)floorl(t2lo*unit)-3, jh=(i64)ceill((t2hi+1)*unit)+3; if(jl<0)jl=0;
            while(jl<=jh && sm(arr(jl,T.S)+K,u)<t2lo) jl++; while(jh>=jl && sm(arr(jh,T.S)+K,u)>t2hi) jh--;
            if(jl>gl)gl=jl; if(jh<gh)gh=jh; }
        if(gl>gh)empty++; else { sw+=gh-gl+1; nw++; } }
    if(meanw)*meanw=nw?sw/nw:0; return empty; }
int main(){ Fsca F; if(!load_fsca(F))return 1; build_tables();
    for(int m=0;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; TABN[m]=n; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0; BASE[m][t+512]=C0TAB[n]+s*T1-s*T3; } }
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; Jc[t+512]=(arr(I1,dx)+K)>>20; }
    static i64 LO[32][1024],HI[32][1024],RLO[32][1024],RHI[32][1024]; double mw;
    for(int m=1;m<32;m++)for(int i=0;i<1024;i++){ int a=(m*1024+(i-512))&0xffff; Win w=decode(F.srz[a],F.srn[a]); if(w.sign>0){RLO[m][i]=w.lo;RHI[m][i]=w.hi;} else {RLO[m][i]=-w.hi;RHI[m][i]=-w.lo;} }
    std::mt19937 rng(99); const int R=10;
    puts("extra J bits g | REAL chip empties | per-word error [-2,+1]   [-4,+2]   [-6,+3]   (mean empties over synthetic chips; min..max)");
    for(G=0;G<=4;G++){ int e=count_empty(RLO,RHI,&mw); printf("      %d        |       %3d         |",G,e);
        for(int amp=0;amp<3;amp++){ double lo_=amp==0?-2:amp==1?-4:-6, hi_=amp==0?1:amp==1?2:3; double se=0; int mn=1<<30,mx=0;
            for(int r=0;r<R;r++){ std::uniform_real_distribution<double> U(lo_,hi_); static double dw[17][1024]; for(int n=0;n<17;n++)for(int i=0;i<1024;i++)dw[n][i]=U(rng);
                for(int m=1;m<32;m++){ const Tab&T=TB[TABN[m]]; for(int i=0;i<1024;i++){ long double P=(long double)(arr(Jc[i],T.S)+K)+(long double)dw[TABN[m]][i]*(long double)T.S; i64 T2=T.S? (i64)floorl(P/(long double)(1ll<<T.u2)):0; if(T2<0)T2=0;
                        i64 V=BASE[m][i]-T2; Win w=decode(to_float(V,1),to_float(V,0)); if(w.sign>0){LO[m][i]=w.lo;HI[m][i]=w.hi;} else {LO[m][i]=-w.hi;HI[m][i]=-w.lo;} } }
                int ee=count_empty(LO,HI,&mw); se+=ee; if(ee<mn)mn=ee; if(ee>mx)mx=ee; }
            printf("   %5.1f (%d..%d)",se/R,mn,mx); }
        puts(""); }
    return 0; }
