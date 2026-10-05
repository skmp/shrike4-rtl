// we_C.cpp -- sweep the stored square constant C together with the latch split, scored against jwin.
//   I1 = latch(arr(C, t<<b) + K1, s1) ; J = latch(arr(I1, t<<a) + K2, s2) ; s1+s2 = 17+a+b
#include "we_win.h"
#include <cstring>
#include <algorithm>
static inline i64 lat(i64 P,int s,int rule){ switch(rule){case 0:return P>>s; case 1:return P>=0?(P>>s):-((-P)>>s); default:return P>=0?(P>>s):-((~P)>>s);} }
int main(int argc,char**argv){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    int RNG=argc>1?atoi(argv[1]):300;
    i64 Ks[4]={1ll<<19,0,1ll<<18,1ll<<20};
    struct R{int sc,dok;i64 C;int p,a,b,k1,k2,r1,r2,u1,u2;};
    std::vector<R> best;
    #pragma omp parallel
    { std::vector<R> loc;
      #pragma omp for schedule(dynamic)
      for(int p=8;p<=13;p++) for(int a=11;a<=14;a++) for(int b=11;b<=14;b++)
        for(int k1=0;k1<4;k1++) for(int k2=0;k2<4;k2++) for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++)
          for(int u1=0;u1<2;u1++) for(int u2=0;u2<2;u2++){
            int s1=b+p, s2=a+17-p; if(s1<1||s2<1||s1>40||s2>40) continue;
            if(Ks[k1]>=(1ll<<s1)||Ks[k2]>=(1ll<<s2)) continue;
            for(i64 C=KC-RNG;C<=KC+RNG;C++){ int s=0,d=0; i64 Jv[1024]; bool bad=false;
              for(int t=-512;t<=511;t++){ i64 d1=(i64)t<<b, d2=(i64)t<<a;
                i64 P1=(r1? arr(d1,C):arr(C,d1))+Ks[k1]; i64 I1=lat(P1,s1,u1);
                if(I1>(1ll<<27)||I1<-(1ll<<27)){bad=true;break;}
                i64 P2=(r2? arr(d2,I1):arr(I1,d2))+Ks[k2]; i64 J=lat(P2,s2,u2); Jv[t+512]=J;
                if(JOK[t+512]&&J>=JLo[t+512]&&J<=JHi[t+512]) s++; }
              if(bad) continue;
              for(int A=1;A<=511;A++){ int ip=A+512,im=-A+512; if(!JOK[ip]||!JOK[im])continue;
                i64 dl=JLo[im]-JHi[ip],dh=JHi[im]-JLo[ip]; if(dl<=0)continue;
                i64 dd=Jv[im]-Jv[ip]; if(dd>=dl&&dd<=dh)d++; }
              if(s>=935) loc.push_back({s,d,C,p,a,b,k1,k2,r1,r2,u1,u2}); } }
      #pragma omp critical
      best.insert(best.end(),loc.begin(),loc.end()); }
    std::sort(best.begin(),best.end(),[](const R&x,const R&y){return x.sc>y.sc;});
    printf("score Dok |  C        C-KC  p  a  b  K1      K2      r1 r2 u1 u2  (s1,s2)\n");
    for(int i=0;i<30&&i<(int)best.size();i++){ R&r=best[i];
        printf("%5d %3d | %8lld %+6lld  %2d %2d %2d %7lld %7lld  %d  %d  %d  %d  (%d,%d)\n",r.sc,r.dok,(long long)r.C,(long long)(r.C-KC),
            r.p,r.a,r.b,(long long)Ks[r.k1],(long long)Ks[r.k2],r.r1,r.r2,r.u1,r.u2,r.b+r.p,r.a+17-r.p); }
    printf("total candidates >=935: %zu\n",best.size());
    return 0; }
