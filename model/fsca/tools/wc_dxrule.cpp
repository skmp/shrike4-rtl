// wc_dxrule.cpp -- exact set of square-path dx offsets that satisfy every node at each t, and whether
// any simple rule lies inside all of them.  (A free square-only dx scored model 12 / jwin 1014.)
#include "wc_core.h"
#include <vector>
#include <algorithm>
static inline i64 chainJ(i64 d){ i64 I1=(arr(KC,d)+K)>>23; return (arr(I1,d)+K)>>20; }
static int OKSET[1024][161];   // offset -80..80 -> violations at that t
int main(){ wc_init();
    #pragma omp parallel for schedule(dynamic)
    for(int t=-512;t<=511;t++){ i64 d0=(i64)t<<13;
        for(int k=-80;k<=80;k++){ i64 Jv=chainJ(d0+k); int v=0;
            for(int nd=1;nd<=31;nd++){ int n=nd>=17?32-nd:nd; if(!TB[n].S)continue;
                i64 V=C0B[n]+LT[nd][t+512]-sm(arr(Jv,TB[n].S)+K,TB[n].u2);
                if(V<WLO[nd][t+512]||V>WHI[nd][t+512])v++; }
            OKSET[t+512][k+80]=v; } }
    int nzero=0, nmin1=0;
    puts("t where offset 0 is NOT optimal: t | viol(0) | best viol | offsets achieving it");
    for(int t=-512;t<=511;t++){ int v0=OKSET[t+512][80]; int bv=1<<30;
        for(int k=0;k<161;k++) bv=std::min(bv,OKSET[t+512][k]);
        if(bv==0) nzero++; if(bv<=1) nmin1++;
        if(v0==bv) continue;
        printf("%5d | %2d | %2d |",t,v0,bv); int c=0;
        for(int k=0;k<161;k++) if(OKSET[t+512][k]==bv && c<14){ printf(" %+d",k-80); c++; }
        puts(""); }
    printf("\nt with a 0-violation offset: %d/1024 ; with <=1: %d\n",nzero,nmin1);
    // which fixed rules work?
    struct Rule{const char*n; i64(*f)(int);};
    auto R0=[](int t)->i64{ return 0; };
    auto R1=[](int t)->i64{ return t<0?-1:0; };
    auto R2=[](int t)->i64{ return t<0?-1:1; };
    auto R3=[](int t)->i64{ return -1; };
    auto R4=[](int t)->i64{ return t<0?-2:0; };
    auto R5=[](int t)->i64{ return t<0?-4:0; };
    auto R6=[](int t)->i64{ return t<0?1:0; };
    struct { const char*n; i64(*f)(int);} rs[] = {{"0",R0},{"-1 for t<0",R1},{"-1/+1",R2},{"-1 always",R3},
        {"-2 for t<0",R4},{"-4 for t<0",R5},{"+1 for t<0",R6}};
    puts("\nfixed offset rules, total violations over all t (sum of per-t violations, 31 node instances):");
    for(auto&r:rs){ int tot=0; for(int t=-512;t<=511;t++){ i64 o=r.f(t); if(o<-80||o>80){tot+=99;continue;} tot+=OKSET[t+512][o+80]; }
        printf("   dx_sq = dx %-14s : %d\n",r.n,tot); }
    // the required offset interval on the t where 0 fails: is it monotone in t?  proportional to t?
    puts("\nfor each failing t: required offset range and offset/t");
    for(int t=-512;t<=511;t++){ if(OKSET[t+512][80]==0) continue; int bv=1<<30;
        for(int k=0;k<161;k++) bv=std::min(bv,OKSET[t+512][k]); if(bv>=OKSET[t+512][80]) continue;
        int lo=999,hi=-999; for(int k=0;k<161;k++) if(OKSET[t+512][k]==bv){ if(k-80<lo)lo=k-80; hi=k-80; }
        printf("%5d  [%+d,%+d]   %.4f\n",t,lo,hi,(lo+hi)/2.0/t); }
    return 0; }
