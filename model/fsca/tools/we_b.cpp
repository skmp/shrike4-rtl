// we_b.cpp -- full two-multiply square-chain family with INDEPENDENT dx shifts in the two multiplies:
//   I1 = latch(arr(KC, t<<b) + K1, b+p)        (W = 2^p is the I1 granularity in KC*t units)
//   J  = latch(arr(I1, t<<a) + K2, a+17-p)
// The shift b decides how many bits of t the first array's row truncation depends on (t mod 2^(16-b)),
// which is the only way the chain can carry information beyond t mod 8.
#include "we_win.h"
#include <cstring>
#include <algorithm>
static inline i64 lat(i64 P,int s,int rule){ if(s<0) return P<<(-s); switch(rule){case 0:return P>>s; case 1:return P>=0?(P>>s):-((-P)>>s); default:return P>=0?(P>>s):-((~P)>>s);} }
int main(){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    i64 Ks[6]={1ll<<19,0,1ll<<18,1ll<<20,1ll<<17,1ll<<16};
    struct Cfg{int p,a,b,k1,k2,r1,r2,u1,u2;};
    std::vector<Cfg> cfgs;
    for(int p=6;p<=12;p++) for(int a=8;a<=15;a++) for(int b=8;b<=15;b++)
      for(int k1=0;k1<6;k1++) for(int k2=0;k2<6;k2++) for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++)
        for(int u1=0;u1<3;u1++) for(int u2=0;u2<3;u2++){
          int s1=b+p, s2=a+17-p; if(s1<1||s2<1||s1>40||s2>40) continue;
          if(Ks[k1]>=(1ll<<s1)||Ks[k2]>=(1ll<<s2)) continue;
          cfgs.push_back({p,a,b,k1,k2,r1,r2,u1,u2}); }
    printf("configs %zu\n",cfgs.size());
    std::vector<int> sc(cfgs.size(),0), dk(cfgs.size(),0), gd(cfgs.size(),0);
    // guard windows: J must also satisfy nodes 1, 2, 16 individually (they are exact for all 1024 t)
    std::vector<i64> GL(1024,0),GH(1024,12000000);
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000;
        for(int node : {1,2,16}){ int n=node; const Tab&T=TB[n]; int u=U2TAB[n];
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+T1-T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo;
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        GL[t+512]=gl; GH[t+512]=gh; }
    #pragma omp parallel for schedule(dynamic,256)
    for(size_t ci=0;ci<cfgs.size();ci++){ Cfg c=cfgs[ci]; int s1=c.b+c.p, s2=c.a+17-c.p; int s=0,d=0;
        i64 Jv[1024];
        for(int t=-512;t<=511;t++){ i64 d1=(i64)t<<c.b, d2=(i64)t<<c.a;
            i64 P1=(c.r1? arr(d1,KC):arr(KC,d1))+Ks[c.k1]; i64 I1=lat(P1,s1,c.u1);
            if(I1>(1ll<<27)||I1<-(1ll<<27)){ s=-1; break; }
            i64 P2=(c.r2? arr(d2,I1):arr(I1,d2))+Ks[c.k2]; i64 J=lat(P2,s2,c.u2); Jv[t+512]=J;
            if(JOK[t+512]&&J>=JLo[t+512]&&J<=JHi[t+512]) s++;
            if(J<GL[t+512]||J>GH[t+512]) gd[ci]++; }
        if(s<0){ sc[ci]=-1; continue; }
        for(int A=1;A<=511;A++){ int ip=A+512,im=-A+512; if(!JOK[ip]||!JOK[im])continue;
            i64 dl=JLo[im]-JHi[ip],dh=JHi[im]-JLo[ip]; if(dl<=0)continue;
            i64 dd=Jv[im]-Jv[ip]; if(dd>=dl&&dd<=dh)d++; }
        sc[ci]=s; dk[ci]=d; }
    std::vector<size_t> idx(cfgs.size()); for(size_t i=0;i<idx.size();i++)idx[i]=i;
    std::vector<size_t> id2; for(size_t i=0;i<cfgs.size();i++) if(sc[i]>=0&&gd[i]==0) id2.push_back(i);
    std::sort(id2.begin(),id2.end(),[&](size_t x,size_t y){return sc[x]>sc[y];});
    printf("GUARD-EXACT configs: %zu ; best:\nscore Dok | p  a  b  K1      K2      r1 r2 u1 u2   (s1,s2)\n",id2.size());
    for(int i=0;i<15&&i<(int)id2.size();i++){ Cfg c=cfgs[id2[i]];
        printf("%5d %3d | %2d %2d %2d %7lld %7lld  %d  %d  %d  %d   (%d,%d)\n",sc[id2[i]],dk[id2[i]],c.p,c.a,c.b,
            (long long)Ks[c.k1],(long long)Ks[c.k2],c.r1,c.r2,c.u1,c.u2,c.b+c.p,c.a+17-c.p); }
    std::sort(idx.begin(),idx.end(),[&](size_t x,size_t y){return sc[x]>sc[y];});
    printf("\nALL configs (guard may break):\nscore Dok | p  a  b  K1      K2      r1 r2 u1 u2   (s1,s2)\n");
    for(int i=0;i<10;i++){ Cfg c=cfgs[idx[i]];
        printf("%5d %3d | %2d %2d %2d %7lld %7lld  %d  %d  %d  %d   (%d,%d)\n",sc[idx[i]],dk[idx[i]],c.p,c.a,c.b,
            (long long)Ks[c.k1],(long long)Ks[c.k2],c.r1,c.r2,c.u1,c.u2,c.b+c.p,c.a+17-c.p); }
    return 0; }
