// wd_w.cpp -- scan the SINE FIELD WIDTH w of the square path.  Scale law: S = RN(sin*2^(w+k)),
// unit column u2 = w+7+k, so every w reproduces the same T2 scale; what changes is the array's
// fixed rounding constant K=2^19 relative to the unit (2^19/2^(w+7+k)).
#include "wd_core.h"
int main(){ if(!load_fsca(FF))return 1;
    const char*rn[4]={"RN","trunc","half-up","ceil"};
    printf(" w  rnd   u2(n=8)  M1    M2  M3   M4    S_8       S_5\n");
    for(int r=0;r<4;r++) for(int w=17;w<=25;w++){
        Cfg c; c.Sw=w; c.Sw_rnd=r; Model M; M.c=c; build_model(M); fit_C0(M); Metrics R=evaluate(M);
        printf("%2d %-8s %2d   %5d %5d %3d %5d   %8lld  %8lld%s\n",w,rn[r],M.u2[8],R.M1,R.M2,R.M3,R.M4,
            (long long)M.S[8],(long long)M.S[5], R.M3==0?"   <== ALL t FEASIBLE":(R.M3<2?"   <== fewer infeasible t":"")); }
    return 0; }
