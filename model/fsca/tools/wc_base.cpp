// wc_base.cpp -- sanity: reproduce the 148 baseline through wc_core, and list the coefficient variants.
#include "wc_core.h"
int main(){ wc_init();
    puts("n   S(v0)   u   C2 true pi^2 (v1)  u    C2 economised (v2)  u   C0range");
    for(int n=0;n<=16;n++) printf("%2d %8lld %3d   %10lld %3d      %10lld %3d   [%lld,%lld] b2=%lld\n",n,
        (long long)CF[0][n].v,CF[0][n].ub,(long long)CF[1][n].v,CF[1][n].ub,(long long)CF[2][n].v,CF[2][n].ub,
        (long long)C0LO[n],(long long)C0HI[n],(long long)C0B[n]);
    int per[32]={0},g=0;
    int tot=wc_score([&](int node,int t)->i64{ int n=node>=17?32-node:node; if(!TB[n].S) return 0;
        i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23, J=(arr(I1,dx)+K)>>20;
        return sm(arr(J,TB[n].S)+K,TB[n].u2); },per,&g);
    printf("\nbaseline chain total=%d (expect 148) guard(nodes 0,1,2,16)=%d\nper node:",tot,g);
    for(int i=0;i<32;i++) printf(" %d:%d",i,per[i]); puts("");
    return 0; }
