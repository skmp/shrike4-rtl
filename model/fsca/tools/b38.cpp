// b38.cpp -- DEFINITIVE solve of the two-multiply square chain with BOTH rounding constants exact.
// As K1 sweeps a full period, each t's first latch takes only two values, so the feasible K2 set per t is a
// union of two intervals selected by K1.  Sweeping K1 through the sorted breakpoints while a segment tree keeps
// the maximum overlap of the K2 intervals gives, for every structure, the exact maximum number of satisfiable t.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 JL[1024],JH[1024]; static char OKT[1024]; static int NF=0;
struct Seg{ std::vector<int> mx,lz; int n;
    void init(int m){ n=1; while(n<m)n<<=1; mx.assign(2*n,0); lz.assign(2*n,0); }
    void upd(int node,int l,int r,int a,int b,int v){ if(b<l||r<a)return;
        if(a<=l&&r<=b){ mx[node]+=v; lz[node]+=v; return; } int m=(l+r)>>1;
        upd(2*node,l,m,a,b,v); upd(2*node+1,m+1,r,a,b,v); mx[node]=std::max(mx[2*node],mx[2*node+1])+lz[node]; }
    void add(int a,int b,int v){ if(a>b)return; upd(1,0,n-1,a,b,v); }
    int best(){ return mx[1]; } };
static int DU=2;
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables(); if(argc>1) DU=atoi(argv[1]);
    printf("latch scale: u2 = 30 + k + %d  (shift %d)\n",DU,20-DU);
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=(12000000ll<<DU);
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n]+DU;
            i64 a1=0,b1=(12000000ll<<DU)+1; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=(12000000ll<<DU); while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JL[t+512]=gl;JH[t+512]=gh;OKT[t+512]=gl<=gh; NF+=OKT[t+512]; }
    printf("feasible t: %d\n",NF);
    struct Cfg{int g1,g2,s1,s2,r1,r2,dk,sg;}; std::vector<Cfg> cfgs;
    for(int g1=8;g1<=14;g1++) for(int g2=8;g2<=14;g2++) for(int s1=10;s1<=34;s1++){
        int s2=g1+g2+17-DU-s1; if(s2<4||s2>34) continue;
        for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(int dk=-3;dk<=3;dk++) for(int sg=0;sg<2;sg++)
            cfgs.push_back({g1,g2,s1,s2,r1,r2,dk,sg}); }
    printf("configs: %zu\n",cfgs.size());
    struct Res{ Cfg c; int best; i64 K1,K2; };
    std::vector<Res> out(cfgs.size());
    #pragma omp parallel for schedule(dynamic)
    for(size_t ci=0;ci<cfgs.size();ci++){ Cfg c=cfgs[ci]; i64 kc=(c.sg?-1:1)*(0x4ef145+c.dk);
        i64 per1=1ll<<c.s1, per2=1ll<<c.s2;
        std::vector<i64> alo(1024),ahi(1024),blo(1024),bhi(1024),bp(1024);
        std::vector<char> aok(1024,0),bok(1024,0);
        std::vector<i64> coords; coords.reserve(4096);
        for(int t=-512;t<=511;t++){ int i=t+512; if(!OKT[i]) continue;
            i64 d1=(i64)t<<c.g1, d2=(i64)t<<c.g2;
            i64 P1 = c.r1? arr(d1,kc) : arr(kc,d1);
            i64 v0 = P1>>c.s1; i64 rem = P1 - (v0<<c.s1);          // rem in [0,2^s1)
            bp[i] = rem? (per1-rem) : per1;                        // K1 at which I1 steps to v0+1
            for(int k=0;k<2;k++){ i64 v=v0+k; i64 Q = c.r2? arr(d2,v) : arr(v,d2);
                i64 lo = JL[i]*per2 - Q, hi = (JH[i]+1)*per2 - 1 - Q;
                if(k==0){ alo[i]=lo; ahi[i]=hi; aok[i]=1; } else { blo[i]=lo; bhi[i]=hi; bok[i]=1; }
                coords.push_back(lo); coords.push_back(hi+1); } }
        std::sort(coords.begin(),coords.end()); coords.erase(std::unique(coords.begin(),coords.end()),coords.end());
        auto idx=[&](i64 v){ return (int)(std::lower_bound(coords.begin(),coords.end(),v)-coords.begin()); };
        Seg sg2; sg2.init((int)coords.size()+1);
        for(int t=-512;t<=511;t++){ int i=t+512; if(!OKT[i]||!aok[i])continue;
            int a=idx(alo[i]), b=idx(ahi[i]+1)-1; sg2.add(a,b,1); }
        int bestv=sg2.best(); i64 bK1=0;
        std::vector<int> ord; for(int t=-512;t<=511;t++){ int i=t+512; if(OKT[i]&&bp[i]<per1) ord.push_back(i); }
        std::sort(ord.begin(),ord.end(),[&](int a,int b){return bp[a]<bp[b];});
        for(size_t q=0;q<ord.size();q++){ int i=ord[q];
            if(aok[i]){ int a=idx(alo[i]),b=idx(ahi[i]+1)-1; sg2.add(a,b,-1); }
            if(bok[i]){ int a=idx(blo[i]),b=idx(bhi[i]+1)-1; sg2.add(a,b,1); }
            if(q+1==ord.size()||bp[ord[q+1]]!=bp[i]){ int v=sg2.best(); if(v>bestv){bestv=v;bK1=bp[i];} } }
        out[ci]={c,bestv,bK1,0}; }
    std::sort(out.begin(),out.end(),[](const Res&a,const Res&b){return a.best>b.best;});
    printf("best | g1 g2 s1 s2 r1 r2 kc      sg  K1\n");
    for(int i=0;i<20;i++){ Res&r=out[i]; printf("%4d | %2d %2d %2d %2d  %d  %d  %06llx %d  %lld\n",
        r.best,r.c.g1,r.c.g2,r.c.s1,r.c.s2,r.c.r1,r.c.r2,(long long)(0x4ef145+r.c.dk),r.c.sg,(long long)r.K1); }
    return 0; }
