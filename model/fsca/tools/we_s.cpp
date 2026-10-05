// we_s.cpp -- score the two-multiply square chain over latch-split s1 (s2 = 43-s1), rounding constants,
// roles and latch rules, against jwin.  Also report, per structure, the number of t it CANNOT reach
// (J granularity) and the 45 forced-D |t| it gets right.
#include "we_win.h"
#include <cstring>
#include <algorithm>
static inline i64 lat(i64 P,int s,int rule){ switch(rule){case 0:return P>>s; case 1:return P>=0?(P>>s):-((-P)>>s); default:return P>=0?(P>>s):-((~P)>>s);} }
int main(){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    i64 K1s[4]={1ll<<19,0,1ll<<18,1ll<<20};
    printf("s1 s2 K1     K2     r1 r2 u1 u2 | score  Dok(of45)\n");
    struct Best{int sc;char line[256];}; std::vector<Best> res;
    for(int s1=17;s1<=29;s1++){ int s2=43-s1; if(s2<6) continue;
      for(int k1=0;k1<4;k1++) for(int k2=0;k2<5;k2++){
        i64 K1=K1s[k1]; i64 K2 = k2<4?K1s[k2]:(1ll<<(s2-1)); if(K2>=(1ll<<s2)) continue;
        for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(int u1=0;u1<3;u1++) for(int u2=0;u2<3;u2++){
          int sc=0,dok=0;
          i64 Jv[1024];
          for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
              i64 P1=(r1? arr(dx,KC):arr(KC,dx))+K1; i64 I1=lat(P1,s1,u1);
              i64 P2=(r2? arr(dx,I1):arr(I1,dx))+K2; i64 J=lat(P2,s2,u2); Jv[t+512]=J;
              if(JOK[t+512]&&J>=JLo[t+512]&&J<=JHi[t+512]) sc++; }
          for(int a=1;a<=511;a++){ int ip=a+512,im=-a+512; if(!JOK[ip]||!JOK[im])continue;
              i64 dl=JLo[im]-JHi[ip], dh=JHi[im]-JLo[ip]; if(dl<=0) continue;   // the 45 forced ones
              i64 d=Jv[im]-Jv[ip]; if(d>=dl&&d<=dh) dok++; }
          Best b; b.sc=sc; snprintf(b.line,sizeof b.line,"%2d %2d %6lld %6lld  %d  %d  %d  %d | %5d  %3d",
              s1,s2,(long long)K1,(long long)K2,r1,r2,u1,u2,sc,dok); res.push_back(b);
        } } }
    std::sort(res.begin(),res.end(),[](const Best&a,const Best&b){return a.sc>b.sc;});
    for(int i=0;i<30&&i<(int)res.size();i++) puts(res[i].line);
    // per-s1 best
    puts("\nbest per s1:");
    for(int s1=17;s1<=29;s1++){ int best=-1; const char*ln=0;
        for(auto&b:res){ int v; sscanf(b.line,"%d",&v); if(v==s1&&b.sc>best){best=b.sc;ln=b.line;} }
        if(ln) puts(ln); }
    return 0; }
