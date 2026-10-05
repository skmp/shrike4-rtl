// wd_res.cpp -- LATCH RESOLUTION scan.  The square latch I2 = (arr(I1,dx)+K)>>s2 and the final unit
// column u2 = (50-s2)+k are tied by the scale law; s2 only sets how finely the latch is quantised.
// s2=20 is the current model.  Smaller s2 = finer latch.
#include "wd_core.h"
int main(){ if(!load_fsca(FF))return 1;
    printf("s2  u2(+k)  maxJ       M1    M2   M3    M4     J(t=17)   window@17\n");
    for(int s2=17;s2<=24;s2++){
        Cfg c; c.i2K=3; c.i2Kv=1ll<<19; c.s2v=s2; c.u2base=50-s2;
        Model M; M.c=c; build_model(M); fit_C0(M); Metrics R=evaluate(M); build_thr(M);
        i64 gl=0,gh=JMAX;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!M.S[n])continue;
            int a=(node*1024+17)&0xffff; i64 l,h;bool e; win_of(a,l,h,e);
            i64 b=base_of(M,n,17,flip); i64 aa,bb; if(!jint(M,n,b-h,b-l,aa,bb)){gl=1;gh=0;break;}
            if(aa>gl)gl=aa; if(bb<gh)gh=bb; }
        printf("%2d   %2d    %9lld  %5d %5d %4d %5d   %8lld   [%lld,%lld]%s\n",s2,50-s2,(long long)JMAX,
            R.M1,R.M2,R.M3,R.M4,(long long)chainJ(17,M.c),(long long)gl,(long long)gh,
            gl>gh?"  EMPTY":"  OK"); }
    return 0; }
