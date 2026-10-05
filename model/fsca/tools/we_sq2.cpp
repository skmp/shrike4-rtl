// we_sq2.cpp -- dx^2-FIRST square path, scored against jwin.
//   A2 = latch(arr(dx,dx) + K1, s)        (t^2 * 2^(26-s) plus the array's own truncation, which is
//                                          sign-asymmetric for 128 of 511 |t|)
//   J  = latch(arr(C, A2) + K2, s2),  s + s2 = 43
#include "we_win.h"
#include <cstring>
#include <algorithm>
static inline i64 lat(i64 P,int s,int rule){ if(s<=0) return P<<(-s); switch(rule){case 0:return P>>s; case 1:return P>=0?(P>>s):-((-P)>>s); default:return P>=0?(P>>s):-((~P)>>s);} }
int main(int argc,char**argv){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    int RNG=argc>1?atoi(argv[1]):8;
    i64 Ks[6]={1ll<<19,0,1ll<<18,1ll<<20,1ll<<17,1ll<<21};
    struct R{int sc,dok;i64 C;int s,s2,k1,k2,r2;};
    std::vector<R> res;
    for(int s=12;s<=28;s++){ int s2=43-s; if(s2<1||s2>40) continue;
      for(int k1=0;k1<6;k1++) for(int k2=0;k2<6;k2++) for(int r2=0;r2<2;r2++){
        if(Ks[k1]>=(1ll<<s)||Ks[k2]>=(1ll<<s2)) continue;
        for(i64 C=KC-RNG;C<=KC+RNG;C++){ int sc=0,dok=0; i64 Jv[1024]; bool bad=false;
          for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 A2=lat(arr(dx,dx)+Ks[k1],s,0); if(A2>(1ll<<27)){bad=true;break;}
            i64 J=lat((r2? arr(A2,C):arr(C,A2))+Ks[k2],s2,0); Jv[t+512]=J;
            if(JOK[t+512]&&J>=JLo[t+512]&&J<=JHi[t+512]) sc++; }
          if(bad) continue;
          for(int a=1;a<=511;a++){ int ip=a+512,im=-a+512; if(!JOK[ip]||!JOK[im])continue;
            i64 dl=JLo[im]-JHi[ip],dh=JHi[im]-JLo[ip]; if(dl<=0)continue;
            i64 dd=Jv[im]-Jv[ip]; if(dd>=dl&&dd<=dh)dok++; }
          res.push_back({sc,dok,C,s,s2,k1,k2,r2}); } } }
    std::sort(res.begin(),res.end(),[](const R&a,const R&b){return a.sc>b.sc;});
    printf("score Dok |  C        C-KC   s  s2  K1      K2      r2\n");
    for(int i=0;i<25&&i<(int)res.size();i++){ R&r=res[i];
        printf("%5d %3d | %8lld %+5lld  %2d %2d %7lld %7lld  %d\n",r.sc,r.dok,(long long)r.C,(long long)(r.C-KC),r.s,r.s2,(long long)Ks[r.k1],(long long)Ks[r.k2],r.r2); }
    return 0; }
