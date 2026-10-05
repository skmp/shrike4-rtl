// wf_k.cpp -- is the dx*dx product itself sign-asymmetric below the 2^26 latch point?
// If arr(dx,dx) != arr(-dx,-dx) then a FINER square latch carries a genuine sign asymmetry while still
// being a square, which is exactly what the data wants (J(-t) >= J(t), never the other way).
// Then: J = (arr(Kq, Qg) + Kr) >> (17+g)  with  Qg = (arr(dx,dx)+Kq1) >> (26-g).
#include "base2.h"
#include <cstdio>
#include <map>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static int OK[1024];
int main(){ FILE*f=fopen("wf_jwin.txt","r"); int t;long long a,b;int o;
  while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OK[t+512]=o;} fclose(f);
  // (1) sign asymmetry of the raw square product
  std::map<i64,int> h; int ndiff=0;
  for(int tt=1;tt<=511;tt++){ i64 dp=(i64)tt<<13, dm=-dp; i64 Ap=arr(dp,dp), Am=arr(dm,dm);
    h[Am-Ap]++; if(Am!=Ap)ndiff++; }
  printf("arr(dx,dx) vs arr(-dx,-dx) over |t|=1..511: differ at %d of 511\n",ndiff);
  printf("histogram of arr(-dx,-dx)-arr(dx,dx):"); for(auto&p:h)printf(" %lld:%d",(long long)p.first,p.second); printf("\n");
  // exact loss of the square product
  { std::map<i64,int> hp,hm; for(int tt=1;tt<=511;tt++){ i64 dp=(i64)tt<<13; hp[dp*dp-arr(dp,dp)]++; hm[dp*dp-arr(-dp,-dp)]++; }
    printf("loss dx^2-arr(dx,dx)  distinct values: %zu   ; for -dx: %zu\n",hp.size(),hm.size());
    printf("  +: "); int c=0; for(auto&p:hp){if(c++<10)printf("%lld(x%d) ",(long long)p.first,p.second);} printf("\n");
    printf("  -: "); c=0; for(auto&p:hm){if(c++<10)printf("%lld(x%d) ",(long long)p.first,p.second);} printf("\n"); }
  // (2) the two-multiply square path dx*dx -> latch -> *Kq
  printf("\n g  shift  best   Kq        Kq1       Kr        role  (Qg granularity in J units)\n");
  int gbestall=-1;
  for(int g=0;g<=9;g++){ int sh1=26-g; if(sh1<0)continue; int sh2=17+g;
    int best=-1; i64 bKq=0,bKq1=0,bKr=0; int brole=0;
    for(int q1=0;q1<4;q1++) for(int role=0;role<2;role++) for(i64 dk=-24;dk<=24;dk++) for(int kr=0;kr<9;kr++){
      i64 Kq1 = q1==0?0 : q1==1?(1ll<<19) : q1==2?(sh1>0?(1ll<<(sh1-1)):0) : (1ll<<16);
      i64 Kq=KC+dk; i64 Kr = kr==0?0 : (1ll<<(kr+12)); if(kr&&Kr>=(1ll<<sh2))continue;
      int sc=0;
      for(int tt=-512;tt<=511;tt++){ if(!OK[tt+512])continue; i64 dx=(i64)tt<<13;
        i64 Q=(arr(dx,dx)+Kq1)>>sh1; if(Q>=(1ll<<27))continue;
        i64 P= role? arr(Q,Kq) : arr(Kq,Q); i64 J=(P+Kr)>>sh2;
        if(J>=JL[tt+512]&&J<=JH[tt+512])sc++; }
      if(sc>best){best=sc;bKq=Kq;bKq1=Kq1;bKr=Kr;brole=role;} }
    printf("%2d  %2d/%2d  %5d  %8lld  %8lld  %8lld  %4d   %.3f\n",g,sh1,sh2,best,(long long)bKq,(long long)bKq1,(long long)bKr,brole,39.47/(1<<g));
    if(best>gbestall)gbestall=best; }
  printf("best over all g: %d\n",gbestall);
  return 0; }
