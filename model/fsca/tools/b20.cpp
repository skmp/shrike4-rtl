// b20.cpp -- COMPLETE exact solve of the two-multiply square chain.
// Key observation: for a latch  I = latch(P + K, s), as K sweeps a full period [0, 2^s) the value I changes
// by at most ONE step, at a single breakpoint.  So each t admits only two candidate I1 values, and for each of
// them the second stage's constant K2 is confined to an exact interval.  Sweeping K1 through the sorted
// breakpoints and maintaining the K2 intersection in multisets therefore solves for BOTH constants exactly.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <set>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 JL[1024],JH[1024]; static char OKT[1024]; static int NFEAS=0;
static inline i64 lat(i64 v,int s,int rule){ switch(rule){ case 0: return v>>s; case 1: return v>=0? (v>>s) : -((-v)>>s);
    default: return v>=0? (v>>s) : -((~v)>>s); } }
struct Best{ int g1,g2,s1,s2,r1,r2,rule1,rule2,dk,sg; i64 K1,K2; int miss; };
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
        JL[t+512]=gl;JH[t+512]=gh;OKT[t+512]=gl<=gh; NFEAS+=OKT[t+512]; }
    printf("feasible t: %d\n",NFEAS);
    struct Cfg{int g1,g2,s1,s2,r1,r2,rule1,rule2,dk,sg;}; std::vector<Cfg> cfgs;
    for(int g1=8;g1<=14;g1++) for(int g2=8;g2<=14;g2++) for(int d=-1;d<=1;d++) for(int s1=8;s1<=34;s1++){
        int s2=g1+g2+17+d-s1; if(s2<4||s2>34) continue;
        for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(int u1=0;u1<3;u1++) for(int u2=0;u2<3;u2++)
          for(int dk=-3;dk<=3;dk++) for(int sg=0;sg<2;sg++) cfgs.push_back({g1,g2,s1,s2,r1,r2,u1,u2,dk,sg}); }
    printf("configs: %zu\n",cfgs.size());
    std::vector<Best> out(cfgs.size());
    #pragma omp parallel for schedule(dynamic)
    for(size_t ci=0;ci<cfgs.size();ci++){ Cfg c=cfgs[ci]; i64 kc=(c.sg?-1:1)*(0x4ef145+c.dk);
        i64 P1[1024]; i64 per1=1ll<<c.s1;
        struct E{ i64 bp; int t; };  std::vector<E> ev; ev.reserve(1024);
        // candidate I1 values at K1=0 and K1=per1-1, plus the breakpoint
        static const int NT=1024; std::vector<i64> v0(NT),v1(NT),bp(NT);
        for(int t=-512;t<=511;t++){ int i=t+512; if(!OKT[i]){v0[i]=v1[i]=0;bp[i]=per1;continue;}
            i64 d1=(i64)t<<c.g1; P1[i]= c.r1? arr(d1,kc):arr(kc,d1);
            v0[i]=lat(P1[i],c.s1,c.rule1); v1[i]=lat(P1[i]+per1-1,c.s1,c.rule1);
            if(v1[i]==v0[i]) bp[i]=per1;
            else { i64 a=1,b=per1-1; while(a<b){ i64 m=a+((b-a)>>1); if(lat(P1[i]+m,c.s1,c.rule1)>v0[i]) b=m; else a=m+1; } bp[i]=a; } }
        // per t and per candidate value, the K2 interval
        auto k2int=[&](int t,i64 v,i64&lo,i64&hi)->bool{ i64 d2=(i64)t<<c.g2; i64 Q=c.r2? arr(d2,v):arr(v,d2);
            i64 per2=1ll<<c.s2;
            // want lat(Q+K2, s2, rule2) in [JL,JH]; solve by monotone search over K2 in [-per2, 2*per2]
            i64 a=-per2,b=2*per2; i64 f=b+1,g=a-1;
            // monotone in K2 -> binary search both ends
            i64 x=a,y=b+1; while(x<y){ i64 m=x+((y-x)>>1); if(lat(Q+m,c.s2,c.rule2)>=JL[t+512]) y=m; else x=m+1; } f=x;
            x=a-1;y=b; while(x<y){ i64 m=x+((y-x+1)>>1); if(lat(Q+m,c.s2,c.rule2)<=JH[t+512]) x=m; else y=m-1; } g=x;
            if(f>g) return false; lo=f;hi=g; return true; };
        std::multiset<i64> los,his; int infeas=0; std::vector<char> cur(NT,0);
        std::vector<i64> alo(NT),ahi(NT),blo(NT),bhi(NT); std::vector<char> aok(NT),bok(NT);
        for(int t=-512;t<=511;t++){ int i=t+512; if(!OKT[i]){aok[i]=bok[i]=1;alo[i]=blo[i]=-(1ll<<60);ahi[i]=bhi[i]=(1ll<<60);continue;}
            aok[i]=k2int(t,v0[i],alo[i],ahi[i]); bok[i]= (bp[i]<per1)? k2int(t,v0[i]+ (v1[i]-v0[i]),blo[i],bhi[i]) : aok[i];
            if(bp[i]>=per1){ bok[i]=aok[i]; blo[i]=alo[i]; bhi[i]=ahi[i]; }
            if(aok[i]){ los.insert(alo[i]); his.insert(ahi[i]); } else infeas++; }
        int bmiss=1<<30; i64 bK1=0,bK2=0;
        auto probe=[&](i64 K1){ if(infeas<bmiss){ i64 lo = los.empty()? -(1ll<<60) : *los.rbegin();
                i64 hi = his.empty()? (1ll<<60) : *his.begin(); if(lo<=hi){ bmiss=infeas; bK1=K1; bK2=lo; } } };
        probe(0);
        std::vector<int> ord; for(int i=0;i<NT;i++) if(bp[i]<per1) ord.push_back(i);
        std::sort(ord.begin(),ord.end(),[&](int a,int b){return bp[a]<bp[b];});
        for(size_t q=0;q<ord.size();q++){ int i=ord[q];
            if(aok[i]){ los.erase(los.find(alo[i])); his.erase(his.find(ahi[i])); } else infeas--;
            if(bok[i]){ los.insert(blo[i]); his.insert(bhi[i]); } else infeas++;
            if(q+1==ord.size()||bp[ord[q+1]]!=bp[i]) probe(bp[i]); }
        out[ci]={c.g1,c.g2,c.s1,c.s2,c.r1,c.r2,c.rule1,c.rule2,c.dk,c.sg,bK1,bK2,bmiss}; }
    std::sort(out.begin(),out.end(),[](const Best&a,const Best&b){return a.miss<b.miss;});
    printf("miss | g1 g2 s1 s2 r1 r2 rule1 rule2 kc      sg  K1        K2\n");
    for(int i=0;i<25;i++){ Best&r=out[i]; printf("%4d | %2d %2d %2d %2d  %d  %d   %d     %d   %06llx %d  %-9lld %lld\n",
        r.miss,r.g1,r.g2,r.s1,r.s2,r.r1,r.r2,r.rule1,r.rule2,(long long)(0x4ef145+r.dk),r.sg,(long long)r.K1,(long long)r.K2); }
    return 0; }
