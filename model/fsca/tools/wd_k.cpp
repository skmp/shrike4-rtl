// wd_k.cpp -- scan the FINAL square product's rounding constant (and the latch shift/unit pair that
// would produce the same effect structurally).  Metrics as in wd_core.h.
#include "wd_core.h"
int main(){ if(!load_fsca(FF))return 1;
    printf("%-30s M1    M2  M3  M4\n","K_sq");
    i64 Ks[]={0,1ll<<16,1ll<<17,1ll<<18,1ll<<19,(1ll<<19)+(1ll<<18),1ll<<20,(1ll<<20)+(1ll<<19),1ll<<21,
              (1ll<<21)+(1ll<<19),(1ll<<21)+(1ll<<20),1ll<<22,1ll<<23,1289544,1300000,1572864,1ll<<24};
    for(i64 k:Ks){ Cfg c; c.sqK=4; c.sqKv=k; Model M; M.c=c; build_model(M); fit_C0(M); Metrics R=evaluate(M);
        printf("K_sq=%-24lld %5d %4d %3d %5d\n",(long long)k,R.M1,R.M2,R.M3,R.M4); }
    printf("\n-- latch shift s2 / unit column u2base pairs with s2+u2base=50 (same total scale) --\n");
    for(int s2=17;s2<=24;s2++){ Cfg c; c.i2K=3; c.i2Kv=1ll<<19; c.s2v=s2; c.u2base=50-s2;
        Model M; M.c=c; build_model(M); fit_C0(M); Metrics R=evaluate(M);
        printf("s2=%2d u2=%2d+k   %5d %4d %3d %5d\n",s2,50-s2,R.M1,R.M2,R.M3,R.M4); }
    return 0; }
