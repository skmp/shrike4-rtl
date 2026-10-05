// b14.cpp -- EXACT inversion of the square chain.
// Given the allowed J intervals, for every (g1,g2,s1,s2,r1,r2,K2-candidate) compute the allowed interval for
// the FIRST latch I1 per t, then solve for the first rounding constant K1 as an exact interval intersection.
// Reports every structure for which a K1 exists that satisfies all 1022 t, and the best near-misses.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 JL[1024],JH[1024]; static char OKT[1024];
struct Res{ int g1,g2,s1,s2,r1,r2; i64 K2,K1lo,K1hi; int hits; };
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n];
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JL[t+512]=gl;JH[t+512]=gh;OKT[t+512]=gl<=gh; }
    // K2 candidate set
    std::vector<i64> K2c; K2c.push_back(0); for(int j=0;j<=30;j++) K2c.push_back(1ll<<j);
    struct Cfg{int g1,g2,s1,r1,r2;int k2i;};
    std::vector<Cfg> cfgs;
    for(int g1=8;g1<=14;g1++) for(int g2=8;g2<=14;g2++) for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++)
        for(int s1=10;s1<=34;s1++){ int s2=g1+g2+17-s1; if(s2<1||s2>40) continue;
            for(size_t k=0;k<K2c.size();k++){ if(K2c[k]>=(1ll<<s2)) continue; cfgs.push_back({g1,g2,s1,r1,r2,(int)k}); } }
    printf("configs: %zu\n",cfgs.size());
    std::vector<Res> out(cfgs.size());
    #pragma omp parallel for schedule(dynamic)
    for(size_t ci=0;ci<cfgs.size();ci++){ Cfg c=cfgs[ci]; int s2=c.g1+c.g2+17-c.s1; i64 K2=K2c[c.k2i];
        i64 kl=-(1ll<<62), kh=(1ll<<62); int hits=0;
        // per t: allowed I1 interval -> allowed K1 interval; intersect
        std::vector<std::pair<i64,i64>> kint; kint.reserve(1024);
        for(int t=-512;t<=511;t++){ if(!OKT[t+512]) continue; i64 d2=(i64)t<<c.g2, d1=(i64)t<<c.g1;
            long double ideal = (long double)KC*(long double)t*powl(2.0L,(long double)c.g1)/powl(2.0L,(long double)c.s1);
            i64 c0=(i64)llroundl(ideal); i64 lo=0,hi=-1; bool any=false;
            for(i64 v=c0-3000;v<=c0+3000;v++){ i64 P2 = c.r2? arr(d2,v) : arr(v,d2); i64 J=(P2+K2)>>s2;
                if(J>=JL[t+512]&&J<=JH[t+512]){ if(!any){lo=v;any=true;} hi=v; } }
            if(!any) continue;                                     // this t can never be satisfied: skip
            i64 P1 = c.r1? arr(d1,KC) : arr(KC,d1);
            i64 a = lo*(1ll<<c.s1) - P1, b = (hi+1)*(1ll<<c.s1) - 1 - P1;   // K1 in [a,b]
            kint.push_back({a,b}); }
        // best K1: the value covered by the most intervals (sweep endpoints)
        std::vector<std::pair<i64,int>> ev; for(auto&p:kint){ ev.push_back({p.first,1}); ev.push_back({p.second+1,-1}); }
        std::sort(ev.begin(),ev.end()); int cur=0,best=0; i64 bl=0,bh=0;
        for(size_t i=0;i<ev.size();i++){ cur+=ev[i].second; if(cur>best&&i+1<ev.size()){ best=cur; bl=ev[i].first; bh=ev[i+1].first-1; } }
        out[ci]={c.g1,c.g2,c.s1,s2,c.r1,c.r2,K2,bl,bh,best}; }
    std::sort(out.begin(),out.end(),[](const Res&a,const Res&b){return a.hits>b.hits;});
    printf("best: (hits out of 1022) g1 g2 s1 s2 r1 r2 K2 -> K1 range\n");
    for(int i=0;i<30;i++){ Res&r=out[i]; printf("%4d | %2d %2d %2d %2d %d %d  K2=%-12lld K1 in [%lld,%lld]\n",
        r.hits,r.g1,r.g2,r.s1,r.s2,r.r1,r.r2,(long long)r.K2,(long long)r.K1lo,(long long)r.K1hi); }
    return 0; }
