// wc_dx.cpp -- does the SQUARE path see a different dx than the linear/cubic?  And how far do richer
// closed forms for J get?  (free square-only dx; sign-dependent dx offset; one's-complement dx;
// 4-parameter floor models with symmetric and antisymmetric |t| terms.)
#include "wc_core.h"
#include <vector>
#include <algorithm>
#include <cmath>
static i64 JLo[1024],JHi[1024]; static char JOK[1024];
static void build_jwin(){
    for(int t=-512;t<=511;t++){ i64 gl=0,gh=12000000;
        for(int node=1;node<=31;node++){ int n=node>=17?32-node:node; if(!TB[n].S)continue; const Tab&T=TB[n];
            i64 base=C0B[n]+LT[node][t+512];
            i64 t2lo=base-WHI[node][t+512], t2hi=base-WLO[node][t+512]; int u=T.u2;
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JLo[t+512]=gl;JHi[t+512]=gh;JOK[t+512]=gl<=gh; } }
static inline i64 chainJ(i64 d){ i64 I1=(arr(KC,d)+K)>>23; return (arr(I1,d)+K)>>20; }
static int jscore_of(const i64*J){ int s=0; for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue;
    if(J[t+512]>=JLo[t+512]&&J[t+512]<=JHi[t+512])s++; } return s; }
int main(){ wc_init(); build_jwin();
    int nf=0; for(int i=0;i<1024;i++) nf+=JOK[i];
    // baseline
    { i64 J[1024]; for(int t=-512;t<=511;t++) J[t+512]=chainJ((i64)t<<13);
      int g=0; printf("baseline chain: jwin %d/%d  model %d\n",jscore_of(J),nf,
        wc_score([&](int nd,int t){int n=nd>=17?32-nd:nd; return TB[n].S? sm(arr(J[t+512],TB[n].S)+K,TB[n].u2):(i64)0;},nullptr,&g)); }
    // (1) free dx for the SQUARE path only
    { i64 J[1024]; i64 bd[1024];
      #pragma omp parallel for schedule(dynamic)
      for(int t=-512;t<=511;t++){ i64 d0=(i64)t<<13; int bv=1<<30; i64 bj=chainJ(d0),bdd=d0;
        for(i64 d=d0-64;d<=d0+64;d++){ i64 Jv=chainJ(d); int v=0;
          for(int nd=1;nd<=31;nd++){ int n=nd>=17?32-nd:nd; if(!TB[n].S)continue;
            i64 V=C0B[n]+LT[nd][t+512]-sm(arr(Jv,TB[n].S)+K,TB[n].u2);
            if(V<WLO[nd][t+512]||V>WHI[nd][t+512])v++; }
          if(v<bv){bv=v;bj=Jv;bdd=d;} }
        J[t+512]=bj; bd[t+512]=bdd; }
      int g=0; printf("(1) free per-t dx for the square path only: jwin %d/%d  model %d\n",jscore_of(J),nf,
        wc_score([&](int nd,int t){int n=nd>=17?32-nd:nd; return TB[n].S? sm(arr(J[t+512],TB[n].S)+K,TB[n].u2):(i64)0;},nullptr,&g));
      printf("    required dx offsets (t, offset) where nonzero, first 40:");
      int c=0; for(int t=-512;t<=511&&c<40;t++){ i64 o=bd[t+512]-((i64)t<<13); if(o){ printf(" (%d,%+lld)",t,(long long)o); c++; } } puts(""); }
    // (2) sign-dependent constant dx offset
    { int bs=0,ba=0,bb=0,bm=1<<30;
      for(int a=-8;a<=8;a++) for(int b=-8;b<=8;b++){ i64 J[1024];
        for(int t=-512;t<=511;t++) J[t+512]=chainJ(((i64)t<<13)+(t<0?a:b));
        int s=jscore_of(J); if(s>bs){bs=s;ba=a;bb=b;
          int g=0; bm=wc_score([&](int nd,int tt){int n=nd>=17?32-nd:nd; return TB[n].S? sm(arr(J[tt+512],TB[n].S)+K,TB[n].u2):(i64)0;},nullptr,&g); } }
      printf("(2) dx += a for t<0, b for t>=0 : best jwin %d/%d at a=%d b=%d (model %d)\n",bs,nf,ba,bb,bm); }
    // (3) 4-parameter closed forms  J = floor((A t^2 + B t + C|t| + R)/2^24)
    { const int SH=24; i64 A0=(i64)KC<<7; int best=0; i64 bA=0,bB=0,bC=0,bR=0;
      #pragma omp parallel
      { int lb=0; i64 lA=0,lB=0,lC=0,lR=0;
        std::vector<std::pair<i64,int>> ev;
        #pragma omp for schedule(dynamic)
        for(int da=-1200;da<=1200;da++){ i64 A=A0+da*16;
          for(i64 B=-300000;B<=300000;B+=6000) for(i64 Cc=-400000;Cc<=400000;Cc+=8000){
            ev.clear();
            for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue; i64 tt=(i64)t*t;
              i64 base=A*tt+B*(i64)t+Cc*(i64)(t<0?-t:t);
              ev.push_back({(JLo[t+512]<<SH)-base,1}); ev.push_back({((JHi[t+512]+1)<<SH)-base,-1}); }
            std::sort(ev.begin(),ev.end()); int cur=0,bst=0; i64 bR2=0;
            for(auto&e:ev){ cur+=e.second; if(cur>bst){bst=cur;bR2=e.first;} }
            if(bst>lb){lb=bst;lA=A;lB=B;lC=Cc;lR=bR2;} } }
        #pragma omp critical
        if(lb>best){best=lb;bA=lA;bB=lB;bC=lC;bR=lR;} }
      printf("(3) J=floor((A t^2 + B t + C|t| + R)/2^24): %d/%d  A=%lld B=%lld C=%lld R=%lld\n",
          best,nf,(long long)bA,(long long)bB,(long long)bC,(long long)bR); }
    return 0; }
