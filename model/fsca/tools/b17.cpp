// b17.cpp -- restructured exact inversion.  The allowed interval for the FIRST latch I1 depends only on the
// SECOND stage (g2,r2,s2,K2), so compute it once per second-stage config and then solve the first stage's
// rounding constant K1 exactly, as an interval intersection, for every (g1,r1,s1,constant) on top of it.
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
struct Best{ int g1,g2,s1,s2,r1,r2,sg,dk; i64 K2,K1lo,K1hi; int hits; };
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
    { int bad=0; for(int t=-512;t<=511;t+=37){ if(!t)continue; i64 d=(i64)t<<13; i64 prev=arr(-3000000,d);
        for(i64 v=-2999999;v<3000000;v+=997){ i64 c=arr(v,d); if(t>0? c<prev : c>prev) bad++; prev=c; } }
      printf("arr(v,d) monotonicity violations (sampled): %d\n",bad); }
    std::vector<i64> K2c; K2c.push_back(0); for(int j=0;j<=32;j++) K2c.push_back(1ll<<j);
    struct S2{int g2,r2,s2;size_t k2;}; std::vector<S2> s2c;
    for(int g2=8;g2<=14;g2++) for(int r2=0;r2<2;r2++) for(int s2=6;s2<=34;s2++)
        for(size_t k=0;k<K2c.size();k++){ if(K2c[k] && K2c[k]>=(1ll<<s2)) continue; s2c.push_back({g2,r2,s2,k}); }
    printf("second-stage configs: %zu\n",s2c.size());
    std::vector<Best> out(s2c.size());
    #pragma omp parallel for schedule(dynamic)
    for(size_t si=0;si<s2c.size();si++){ S2 c=s2c[si]; i64 K2=K2c[c.k2];
        std::vector<i64> il(1024),ih(1024); std::vector<char> okk(1024,0);
        for(int t=-512;t<=511;t++){ if(!OKT[t+512]) continue; i64 d2=(i64)t<<c.g2;
            auto Jof=[&](i64 v){ i64 P=c.r2? arr(d2,v):arr(v,d2); return (P+K2)>>c.s2; };
            if(t==0){ i64 J=Jof(0); if(J>=JL[512]&&J<=JH[512]){ il[512]=-(1ll<<40); ih[512]=(1ll<<40); okk[512]=1; } continue; }
            i64 lim=6000000; bool inc = Jof(1000) > Jof(0);
            i64 a1,b1,a2,b2;
            if(inc){ a1=-lim;b1=lim+1; while(a1<b1){i64 m=a1+((b1-a1)>>1); if(Jof(m)>=JL[t+512])b1=m;else a1=m+1;}
                     a2=-lim-1;b2=lim; while(a2<b2){i64 m=a2+((b2-a2+1)>>1); if(Jof(m)<=JH[t+512])a2=m;else b2=m-1;}
                     if(a1<=a2){il[t+512]=a1;ih[t+512]=a2;okk[t+512]=1;} }
            else   { a1=-lim;b1=lim+1; while(a1<b1){i64 m=a1+((b1-a1)>>1); if(Jof(m)<=JH[t+512])b1=m;else a1=m+1;}
                     a2=-lim-1;b2=lim; while(a2<b2){i64 m=a2+((b2-a2+1)>>1); if(Jof(m)>=JL[t+512])a2=m;else b2=m-1;}
                     if(a1<=a2){il[t+512]=a1;ih[t+512]=a2;okk[t+512]=1;} } }
        Best bb; bb.hits=-1;
        for(int g1=8;g1<=14;g1++){ int s1=g1+c.g2+17-c.s2; if(s1<6||s1>40) continue;
          for(int r1=0;r1<2;r1++) for(int sg=0;sg<2;sg++) for(int dk=-4;dk<=4;dk++){
            i64 kc=(sg?-1:1)*(0x4ef145+dk);
            std::vector<std::pair<i64,i64>> kint; kint.reserve(1024);
            for(int t=-512;t<=511;t++){ if(!okk[t+512]||t==0) continue;
                i64 d1=(i64)t<<g1; i64 P1 = r1? arr(d1,kc) : arr(kc,d1);
                kint.push_back({ il[t+512]*(1ll<<s1) - P1, (ih[t+512]+1)*(1ll<<s1) - 1 - P1 }); }
            std::vector<std::pair<i64,int>> ev; ev.reserve(kint.size()*2);
            for(auto&p:kint){ ev.push_back({p.first,1}); ev.push_back({p.second+1,-1}); }
            std::sort(ev.begin(),ev.end()); int cur=0,best=0; i64 bl=0,bh=0;
            for(size_t i=0;i<ev.size();i++){ cur+=ev[i].second; if(cur>best&&i+1<ev.size()){best=cur;bl=ev[i].first;bh=ev[i+1].first-1;} }
            if(best>bb.hits) bb={g1,c.g2,s1,c.s2,r1,c.r2,sg,dk,K2,bl,bh,best}; } }
        out[si]=bb; }
    std::sort(out.begin(),out.end(),[](const Best&a,const Best&b){return a.hits>b.hits;});
    printf("hits/1021 | g1 g2 s1 s2 r1 r2 sgn kc      K2          K1 range\n");
    for(int i=0;i<30;i++){ Best&r=out[i]; if(r.hits<0)continue;
        printf("%5d     | %2d %2d %2d %2d  %d  %d   %d %06llx  %-11lld [%lld,%lld]\n",r.hits,r.g1,r.g2,r.s1,r.s2,r.r1,r.r2,
            r.sg,(long long)(0x4ef145+r.dk),(long long)r.K2,(long long)r.K1lo,(long long)r.K1hi); }
    return 0; }
