// wc_floor.cpp -- structural floors for the square term, computed on the RAW windows of all 31 node
// instances.  Nothing about the square's internal form is assumed except where stated.
//   (A) free per-(node,t) T2  -> 0 by construction unless the window itself is empty (sanity on C0/u2)
//   (B) T2 EVEN in t, chosen per node independently  -> the true floor for ANY t-even square term
//   (C) T2 even in t but node 16's t>=0 side EXCLUDED (quadrant-swap hypothesis)
//   (D) shared latch J(t) free           (expect 2)
//   (E) shared latch J(t) forced even    (expect 70)
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
// T2 window per (node,t): T2 must be in [T2lo,T2hi]; empty => impossible
static i64 T2LO[32][1024], T2HI[32][1024];
int main(){ if(!load_fsca(F))return 1; build_tables();
    int empt=0;
    for(int node=0;node<32;node++){ int flip=node>=17,n=flip?32-node:node;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            T2LO[node][t+512]=base-hi; T2HI[node][t+512]=base-lo;
            if(base-hi>base-lo) empt++; } }
    printf("(A) empty T2 windows (should be 0): %d\n", empt);
    // (B) per-node even floor
    int flB=0; std::vector<std::pair<int,int>> bad;
    for(int node=1;node<32;node++){ int n=node>=17?32-node:node; if(!TB[n].S) continue;
        for(int a=1;a<=511;a++){ int ip=a+512, im=-a+512;
            i64 lo=std::max(T2LO[node][ip],T2LO[node][im]), hi=std::min(T2HI[node][ip],T2HI[node][im]);
            if(lo>hi){ flB++; bad.push_back({node,a}); } } }
    printf("(B) per-node T2-even floor: %d violating (node,|t|) pairs\n", flB);
    { int per[32]={0}; for(auto&p:bad) per[p.first]++;
      printf("    by node:"); for(int i=0;i<32;i++) if(per[i]) printf(" %d:%d",i,per[i]); puts(""); }
    printf("    list:"); for(auto&p:bad) printf(" (%d,%d)",p.first,p.second); puts("");
    // (C) same, but drop node 16's t>=0 half (and node 0's? node 0 has S=0 so no square)
    int flC=0; for(auto&p:bad) if(p.first!=16) flC++;
    printf("(C) per-node T2-even floor excluding ALL of node 16: %d\n", flC);
    // (D)/(E): shared latch
    struct C{ i64 lo,hi; i64 S; int u; };
    std::vector<C> con[1024];
    for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
        for(int t=-512;t<=511;t++) con[t+512].push_back({T2LO[node][t+512],T2HI[node][t+512],TB[n].S,U2TAB[n]}); }
    auto viol=[&](int t,i64 J){ int v=0; for(auto&c:con[t+512]){ i64 T2=sm(arr(J,c.S)+K,c.u); if(T2<c.lo||T2>c.hi)v++; } return v; };
    std::vector<std::vector<i64>> cand(1024);
    int totD=0;
    for(int t=-512;t<=511;t++){ std::vector<i64>&cd=cand[t+512];
        for(auto&c:con[t+512]){ i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,c.S)+K,c.u)>=c.lo)b1=m;else a1=m+1;} cd.push_back(a1);
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,c.S)+K,c.u)<=c.hi)a2=m;else b2=m-1;} cd.push_back(a2); }
        std::sort(cd.begin(),cd.end()); cd.erase(std::unique(cd.begin(),cd.end()),cd.end());
        int bv=1<<30; for(i64 j:cd){ if(j<0)continue; int v=viol(t,j); if(v<bv)bv=v; } totD+=bv;
        if(bv) printf("    (D) t=%d needs %d violations\n",t,bv); }
    printf("(D) free shared J(t): %d\n", totD);
    int totE=0; std::vector<std::pair<int,int>> ebad;
    for(int a=0;a<=511;a++){ int ip=a+512, im=(a?-a:0)+512;
        std::vector<i64> cd=cand[ip]; for(i64 v:cand[im])cd.push_back(v); std::sort(cd.begin(),cd.end()); cd.erase(std::unique(cd.begin(),cd.end()),cd.end());
        int bv=1<<30; for(i64 j:cd){ if(j<0)continue; int v=viol(a,j)+(a?viol(-a,j):0); if(v<bv)bv=v; }
        totE+=bv; if(bv) ebad.push_back({a,bv}); }
    printf("(E) shared J even in t: %d\n", totE);
    printf("    where:"); for(auto&p:ebad) printf(" %d:%d",p.first,p.second); puts("");
    return 0; }
