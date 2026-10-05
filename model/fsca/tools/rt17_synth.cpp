// rt17_synth.cpp -- SYNTHETIC-CHIP null test.  Build a fake hardware whose square latch is the chain's J plus a
// NODE-INDEPENDENT per-offset error e(t), encode V to RN/RZ floats, decode the windows exactly as for the real dump,
// and score the noiseless chain against it.  Tune the noise so the total matches the real residual (~148), then ask:
// how many violations land on the guard nodes {1,2,16,30,31}?   Real chip: 0.
#include "base2.h"
#include <random>
#include <algorithm>
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static i64 BASE[32][1024]; static int TABN[32];
int main(){ build_tables();
    for(int m=0;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; TABN[m]=n; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0; BASE[m][t+512]=C0TAB[n]+s*T1-s*T3; } }
    static i64 Jc[1024]; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; Jc[t+512]=(arr(I1,dx)+K)>>18; }
    std::mt19937 rng(777);
    struct NM { const char* nm; int lo, hi; }; NM nms[]={{"uniform [-8,+4] fine",-8,4},{"uniform [-16,+8] fine",-16,8},{"uniform [-24,+12] fine",-24,12},{"uniform [-32,+16] fine",-32,16},{"uniform [-16,0] (pure extra truncation, 4 coarse)",-16,0},{"uniform [-32,0] (8 coarse)",-32,0}};
    puts("noise on J (node-independent)                          total(mean)  guard(mean)  guard 5%..95%   P(guard==0)   node16(mean)  nodes1,2,30,31(mean)");
    for(auto&nm:nms){ const int R=60; std::vector<int> G; double st=0,sg=0,s16=0,s12=0; int z=0;
        for(int r=0;r<R;r++){ static i64 Jt[1024]; std::uniform_int_distribution<int> U(nm.lo,nm.hi); for(int i=0;i<1024;i++){ Jt[i]=Jc[i]+U(rng); if(Jt[i]<0)Jt[i]=0; }
            int tot=0,g=0,n16=0,n12=0;
            for(int m=1;m<32;m++){ const Tab&T=TB[TABN[m]]; bool gd=(m<=2||m==16||m>=30);
                for(int i=0;i<1024;i++){ i64 Vt=BASE[m][i]-sm(arr(Jt[i],T.S)+K,T.u2+2); i64 Vm=BASE[m][i]-sm(arr(Jc[i],T.S)+K,T.u2+2); if(Vt==Vm)continue;
                    Win w=decode(to_float(Vt,1),to_float(Vt,0)); i64 lo=w.sign>0?w.lo:-w.hi, hi=w.sign>0?w.hi:-w.lo; if(Vm<lo||Vm>hi){ tot++; if(gd){g++; if(m==16)n16++; else n12++;} } } }
            st+=tot; sg+=g; s16+=n16; s12+=n12; z+=g==0; G.push_back(g); }
        std::sort(G.begin(),G.end()); printf("%-52s  %8.1f    %8.2f       %2d..%-2d        %5.2f        %6.2f        %6.2f\n",nm.nm,st/R,sg/R,G[R/20],G[R*19/20],(double)z/R,s16/R,s12/R); }
    puts("real chip vs the chain:                                   148 (coarse) / 161 (fine)      0 / 2");
    return 0; }
