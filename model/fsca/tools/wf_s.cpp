// wf_s.cpp -- the square latch as ONE multiply on the exact t^2 latch, plus a small ODD term inside the
// same truncation.  The array's row truncation below column 16 is an ABSOLUTE loss of up to ~2^19, i.e. up
// to ~4 units once divided by 2^17 -- exactly the size of the observed deviation at every |t|, which is why
// the two-stage chain (whose loss scales with |t|) cannot fit.  A small odd term inside the floor gives a
// sign asymmetry of 0 or 1 unit, which is what 43 of the 45 forced asymmetries actually demand.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static int OK[1024];
int main(){ FILE*f=fopen("wf_jwin.txt","r"); int t;long long a,b;int o;
  while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OK[t+512]=o;} fclose(f);
  // 1. pure arithmetic: J = floor((Q*t^2 + B*t + C)/1), Q,B,C real, B on a FINE grid
  { int best=-1; long double bQ=0,bB=0,bC=0; long double Q0=(long double)KC/131072.0L;
    #pragma omp parallel
    { int lb=-1; long double lQ=0,lB=0,lC=0;
      #pragma omp for schedule(dynamic)
      for(int qi=-3000;qi<=3000;qi++){ long double Q=Q0+qi*3e-6L;
        for(int bi=-600;bi<=600;bi++){ long double B=bi*2e-5L;
          std::vector<std::pair<long double,int>> ev; ev.reserve(2044);
          for(int tt=-512;tt<=511;tt++){ if(!OK[tt+512])continue; long double base=Q*(long double)tt*tt+B*tt;
            ev.push_back({(long double)JL[tt+512]-base,1}); ev.push_back({(long double)JH[tt+512]+1-base,-1}); }
          std::sort(ev.begin(),ev.end()); int cur=0,mx=0; long double bc=0;
          for(size_t i=0;i<ev.size();i++){cur+=ev[i].second; if(cur>mx){mx=cur;bc=ev[i].first;}}
          if(mx>lb){lb=mx;lQ=Q;lB=B;lC=bc;} } }
      #pragma omp critical
      { if(lb>best){best=lb;bQ=lQ;bB=lB;bC=lC;} } }
    printf("J = floor(Q t^2 + B t + C)  fine B grid : %4d / 1022   Q=%.8Lf  B=%.6Lf (= %.1Lf / 2^17)  C=%.4Lf\n",
      best,bQ,bB,bB*131072.0L,bC); }
  // 2. through the array, with an odd term inside the same truncation
  { int best=-1; i64 bKq=0,bKr=0,bBeta=0; int bg=0,brole=0;
    #pragma omp parallel
    { int lb=-1; i64 lKq=0,lKr=0,lBeta=0; int lg=0,lr=0;
      #pragma omp for schedule(dynamic)
      for(int beta=-6000;beta<=6000;beta++)
        for(int g=0;g<=2;g++) for(int role=0;role<2;role++) for(i64 dk=-8;dk<=8;dk++) for(int kr=0;kr<10;kr++){
          i64 Kq=KC+dk; int sh=17+g; i64 Kr=kr==0?0:(1ll<<(kr+9)); if(kr&&Kr>=(1ll<<sh))continue; int sc=0;
          for(int tt=-512;tt<=511;tt++){ if(!OK[tt+512])continue; i64 sq=((i64)tt*tt)<<g;
            i64 P=(role?arr(sq,Kq):arr(Kq,sq)) + Kr + (i64)beta*tt; i64 J=P>>sh;
            if(J>=JL[tt+512]&&J<=JH[tt+512])sc++; }
          if(sc>lb){lb=sc;lKq=Kq;lKr=Kr;lBeta=beta;lg=g;lr=role;} }
      #pragma omp critical
      { if(lb>best){best=lb;bKq=lKq;bKr=lKr;bBeta=lBeta;bg=lg;brole=lr;} } }
    printf("J = (arr(Kq,t^2<<g) + Kr + beta*t) >> (17+g) : %4d / 1022   Kq=%lld Kr=%lld beta=%lld g=%d role=%d\n",
      best,(long long)bKq,(long long)bKr,(long long)bBeta,bg,brole);
    // misses of the best
    int miss=0,neg=0,pos=0;
    for(int tt=-512;tt<=511;tt++){ if(!OK[tt+512])continue; i64 sq=((i64)tt*tt)<<bg;
      i64 P=(brole?arr(sq,bKq):arr(bKq,sq)) + bKr + bBeta*tt; i64 J=P>>(17+bg);
      if(J<JL[tt+512]||J>JH[tt+512]){ miss++; if(tt<0)neg++; else pos++; } }
    printf("   misses %d (t<0: %d, t>=0: %d)\n",miss,neg,pos); }
  // 3. odd term plus an explicit one's-complement style +gamma on the negative side
  { int best=-1; i64 bKq=0,bKr=0,bBeta=0,bGam=0;
    #pragma omp parallel
    { int lb=-1; i64 lKq=0,lKr=0,lBeta=0,lGam=0;
      #pragma omp for schedule(dynamic)
      for(int beta=-4000;beta<=4000;beta++) for(int gam=0;gam<=6;gam++)
        for(i64 dk=-6;dk<=6;dk++) for(int kr=0;kr<10;kr++){
          i64 Kq=KC+dk; i64 Kr=kr==0?0:(1ll<<(kr+9)); if(kr&&Kr>=(1ll<<17))continue; int sc=0;
          for(int tt=-512;tt<=511;tt++){ if(!OK[tt+512])continue;
            i64 P=arr(Kq,(i64)tt*tt) + Kr + (i64)beta*tt; i64 J=(P>>17) + (tt<0?gam:0);
            if(J>=JL[tt+512]&&J<=JH[tt+512])sc++; }
          if(sc>lb){lb=sc;lKq=Kq;lKr=Kr;lBeta=beta;lGam=gam;} }
      #pragma omp critical
      { if(lb>best){best=lb;bKq=lKq;bKr=lKr;bBeta=lBeta;bGam=lGam;} } }
    printf("J = (arr(Kq,t^2)+Kr+beta*t)>>17 + gamma*[t<0] : %4d / 1022   Kq=%lld Kr=%lld beta=%lld gamma=%lld\n",
      best,(long long)bKq,(long long)bKr,(long long)bBeta,(long long)bGam); }
  return 0; }
