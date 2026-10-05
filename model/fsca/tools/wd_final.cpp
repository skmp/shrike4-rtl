// wd_final.cpp -- full end-to-end evaluation of the finer-latch model over nodes 0..31.
//   I1 = (arr(KC,dx)+k1)>>s1 ; I2 = (arr(I1,dx)+k2)>>s2 ; T2 = sm(arr(I2,S)+2^19, (50-s2)+k)
#include "wd_core.h"
int main(){ if(!load_fsca(FF))return 1;
    printf("s2 u2  k1        k2        M1   M2 M3  M4   guard(0,1,2,16)  per-node violations\n");
    for(int s2=17;s2<=20;s2++)
    for(i64 k1 : {0ll,1ll<<18,1ll<<19,1ll<<20,1ll<<21})
    for(i64 k2 : {0ll,1ll<<16,1ll<<17,1ll<<18,1ll<<19}){
        Cfg c; c.i1K=(k1==(1ll<<19))?0:(k1==0?1:2); c.i2K=3; c.i2Kv=k2; c.s2v=s2; c.u2base=50-s2;
        if(k1!=0&&k1!=(1ll<<19)&&k1!=(1ll<<22)) { if(k1==(1ll<<18)||k1==(1ll<<20)||k1==(1ll<<21)) continue; }
        Model M; M.c=c; build_model(M); fit_C0(M); Metrics R=evaluate(M);
        bool g = R.nodeviol[0]==0&&R.nodeviol[1]==0&&R.nodeviol[2]==0&&R.nodeviol[16]==0
              && R.nodeviol[30]==0&&R.nodeviol[31]==0;
        printf("%2d %2d  %-9lld %-9lld %4d %3d %2d %4d  %s  ",s2,50-s2,(long long)k1,(long long)k2,R.M1,R.M2,R.M3,R.M4,g?"EXACT":"broken");
        for(int i=0;i<32;i++) if(R.nodeviol[i]) printf("%d:%d ",i,R.nodeviol[i]); printf("\n"); }
    return 0; }
