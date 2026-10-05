// we_cap.cpp -- CAPABILITY ceiling of the second square multiply.
// For each candidate last-chain-stage  J = latch(arr(rec,mul) + K2, s2), {rec,mul} = {I1, t<<a},
// count t for which SOME integer I1 lands J in the window.  score <= that count.
#include "we_win.h"
#include <cstring>
#include <algorithm>
struct Cfg{int a,s2,role,rule;i64 K2;};
static inline i64 Jv(const Cfg&c,i64 v,i64 dx){ i64 P=(c.role? arr(dx,v):arr(v,dx))+c.K2;
    switch(c.rule){case 0: return P>>c.s2; case 1: return P>=0?(P>>c.s2):-((-P)>>c.s2); default: return P>=0?(P>>c.s2):-((~P)>>c.s2);} }
int main(){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    std::vector<Cfg> cfgs;
    std::vector<i64> K2s={1ll<<19,0,1ll<<18,1ll<<17,1ll<<20,1ll<<16};
    for(int a=10;a<=16;a++) for(int s2=10;s2<=30;s2++) for(int role=0;role<2;role++) for(int rule=0;rule<3;rule++)
        for(i64 k:K2s){ if(k>=(1ll<<s2)) continue; cfgs.push_back({a,s2,role,rule,k}); }
    std::vector<int> cap(cfgs.size());
    #pragma omp parallel for schedule(dynamic)
    for(size_t ci=0;ci<cfgs.size();ci++){ Cfg c=cfgs[ci]; int good=0;
        for(int t=-512;t<=511;t++){ if(!JOK[t+512]) continue; i64 dx=(i64)t<<c.a; if(!dx){good++;continue;}
            int sg = t<0?-1:1; i64 lo=0,hi=(i64)1<<27;
            while(lo<hi){ i64 m=lo+(hi-lo)/2; if(Jv(c,sg*m,dx)>=JLo[t+512]) hi=m; else lo=m+1; }
            bool any=false;
            for(i64 m=std::max((i64)0,lo-24);m<=lo+24&&!any;m++){ i64 j=Jv(c,sg*m,dx); if(j>=JLo[t+512]&&j<=JHi[t+512]) any=true; }
            if(any) good++; }
        cap[ci]=good; }
    std::vector<size_t> idx(cfgs.size()); for(size_t i=0;i<idx.size();i++)idx[i]=i;
    std::sort(idx.begin(),idx.end(),[&](size_t x,size_t y){return cap[x]>cap[y];});
    printf("cap  | a  s2 role rule K2\n");
    int n=0; for(size_t i:idx){ if(n++>=45) break; Cfg c=cfgs[i]; printf("%4d | %2d %2d  %d    %d   %lld\n",cap[i],c.a,c.s2,c.role,c.rule,(long long)c.K2); }
    for(size_t i=0;i<cfgs.size();i++){ Cfg c=cfgs[i]; if(c.a==13&&c.s2==20&&c.role==0&&c.rule==0&&c.K2==(1ll<<19)) printf("\nreference a=13,s2=20,role0,rule0,K2=2^19: cap=%d\n",cap[i]); }
    return 0; }
