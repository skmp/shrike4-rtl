// we_bestfull.cpp -- among all GUARD-EXACT two-multiply square-latch rules, minimise the end-to-end
// window-violation count over nodes 0..31 (the 148 baseline metric).
#include "we_win.h"
#include <cstring>
#include <algorithm>
static inline i64 lat(i64 P,int s,int rule){ if(s<=0) return P<<(-s); switch(rule){case 0:return P>>s; case 1:return P>=0?(P>>s):-((-P)>>s); default:return P>=0?(P>>s):-((~P)>>s);} }
struct Inst{int n,flip;i64 lo,hi;}; static std::vector<Inst> ins[1024];
int main(){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    for(int t=-512;t<=511;t++) for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
        int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex); ins[t+512].push_back({n,flip,lo,hi}); }
    // precompute the t-independent parts
    static i64 BASE[32][1024];
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
        for(int t=-512;t<=511;t++){ const Tab&T=TB[n]; i64 dx=(i64)t<<13;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 s=flip?-1:1; BASE[node][t+512]=C0TAB[n]+s*T1-s*T3; } }
    i64 Ks[6]={1ll<<19,0,1ll<<18,1ll<<20,1ll<<17,1ll<<16};
    struct Cfg{int p,a,b,k1,k2,r1,r2,u1,u2;};
    std::vector<Cfg> cfgs;
    for(int p=6;p<=12;p++) for(int a=8;a<=16;a++) for(int b=8;b<=16;b++)
      for(int k1=0;k1<6;k1++) for(int k2=0;k2<6;k2++) for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++)
        for(int u1=0;u1<3;u1++) for(int u2=0;u2<3;u2++){
          int s1=b+p, s2=a+17-p; if(s1<1||s2<1||s1>40||s2>40) continue;
          if(Ks[k1]>=(1ll<<s1)||Ks[k2]>=(1ll<<s2)) continue; cfgs.push_back({p,a,b,k1,k2,r1,r2,u1,u2}); }
    printf("configs %zu\n",cfgs.size());
    std::vector<int> viol(cfgs.size(),1<<20), jw(cfgs.size(),0);
    #pragma omp parallel for schedule(dynamic,128)
    for(size_t ci=0;ci<cfgs.size();ci++){ Cfg c=cfgs[ci]; int s1=c.b+c.p, s2=c.a+17-c.p;
        i64 Jv[1024]; bool bad=false;
        for(int t=-512;t<=511;t++){ i64 d1=(i64)t<<c.b, d2=(i64)t<<c.a;
            i64 I1=lat((c.r1? arr(d1,KC):arr(KC,d1))+Ks[c.k1],s1,c.u1);
            if(I1>(1ll<<27)||I1<-(1ll<<27)){bad=true;break;}
            Jv[t+512]=lat((c.r2? arr(d2,I1):arr(I1,d2))+Ks[c.k2],s2,c.u2); }
        if(bad) continue;
        int g=0,tot=0,js=0;
        for(int node : {0,1,2,16}){ int n=node; for(int t=-512;t<=511;t++){ const Tab&T=TB[n];
            i64 T2=T.S? sm(arr(Jv[t+512],T.S)+K,U2TAB[n]):0; i64 V=BASE[node][t+512]-T2;
            for(auto&I:ins[t+512]) if(I.n==n&&I.flip==(node>=17)){ if(V<I.lo||V>I.hi)g++; break; } } if(g)break; }
        if(g) continue;
        for(int t=-512;t<=511;t++){ if(JOK[t+512]&&Jv[t+512]>=JLo[t+512]&&Jv[t+512]<=JHi[t+512]) js++;
            for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n];
                i64 T2=T.S? sm(arr(Jv[t+512],T.S)+K,U2TAB[n]):0; i64 V=BASE[node][t+512]-T2;
                for(auto&I:ins[t+512]) if(I.n==n&&I.flip==flip){ if(V<I.lo||V>I.hi)tot++; break; } } }
        viol[ci]=tot; jw[ci]=js; }
    std::vector<size_t> idx; for(size_t i=0;i<cfgs.size();i++) if(viol[i]<(1<<20)) idx.push_back(i);
    std::sort(idx.begin(),idx.end(),[&](size_t x,size_t y){return viol[x]<viol[y];});
    printf("guard-exact configs: %zu\nviol jwin | p  a  b  K1      K2      r1 r2 u1 u2  (s1,s2)\n",idx.size());
    for(int i=0;i<20&&i<(int)idx.size();i++){ Cfg c=cfgs[idx[i]];
        printf("%4d %4d | %2d %2d %2d %7lld %7lld  %d  %d  %d  %d  (%d,%d)\n",viol[idx[i]],jw[idx[i]],c.p,c.a,c.b,
            (long long)Ks[c.k1],(long long)Ks[c.k2],c.r1,c.r2,c.u1,c.u2,c.b+c.p,c.a+17-c.p); }
    return 0; }
