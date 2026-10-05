// b10.cpp -- (1) is the hardware's dx^2 latch exact?  (2) minimum achievable violations with a free J(t),
// (3) same with J forced EVEN in t, (4) per-(order,s1) best chain scores.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
struct C{ i64 lo,hi; i64 S; int u; };      // T2 must be in [lo,hi]
static std::vector<C> con[1024];           // per t
int main(){ if(!load_fsca(F))return 1; build_tables();
    // (1)
    int bad=0; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; if(((arr(dx,dx)+K)>>26)!=(i64)t*t) bad++; }
    printf("(1) floor((arr(dx,dx)+2^19)/2^26) != t^2 : %d/1024\n",bad);
    int bad2=0; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; if((arr(dx,dx)>>26)!=(i64)t*t) bad2++; }
    printf("    without the rounding constant: %d/1024\n",bad2);
    // constraints
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            con[t+512].push_back({base-hi,base-lo,T.S,U2TAB[n]}); } }
    // (2) free J per t: minimise violations over J in [0,12e6]  (scan candidate boundaries)
    auto viol=[&](int t,i64 J){ int v=0; for(auto&c:con[t+512]){ i64 T2=sm(arr(J,c.S)+K,c.u); if(T2<c.lo||T2>c.hi)v++; } return v; };
    // candidate J values: for each constraint, the interval endpoints (binary search), plus the model
    int tot_free=0, tot_even=0; std::vector<i64> bestJ(1024);
    std::vector<std::vector<i64>> cand(1024);
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23, Jm=(arr(I1,dx)+K)>>20;
        std::vector<i64>&cd=cand[t+512];
        for(auto&c:con[t+512]){ i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,c.S)+K,c.u)>=c.lo)b1=m;else a1=m+1;} cd.push_back(a1);
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,c.S)+K,c.u)<=c.hi)a2=m;else b2=m-1;} cd.push_back(a2); }
        cd.push_back(Jm); std::sort(cd.begin(),cd.end()); cd.erase(std::unique(cd.begin(),cd.end()),cd.end());
        int bv=1<<30; i64 bj=Jm; for(i64 j:cd){ if(j<0)continue; int v=viol(t,j); if(v<bv){bv=v;bj=j;} }
        tot_free+=bv; bestJ[t+512]=bj; }
    printf("(2) minimum violations with a completely FREE J(t): %d  (current model: 148)\n",tot_free);
    // (3) J even in t
    for(int a=0;a<=511;a++){ int ip=a+512, im=(a?-a:0)+512;
        std::vector<i64> cd=cand[ip]; for(i64 v:cand[im])cd.push_back(v); std::sort(cd.begin(),cd.end()); cd.erase(std::unique(cd.begin(),cd.end()),cd.end());
        int bv=1<<30; for(i64 j:cd){ if(j<0)continue; int v=viol(a,j)+(a? viol(-a,j):0); if(v<bv)bv=v; }
        tot_even+=bv; }
    printf("(3) minimum violations with J EVEN in t: %d\n",tot_even);
    return 0; }
