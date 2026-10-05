// wf_n.cpp -- how much of the latch is a SMOOTH function of t?  Maximum number of the 1022 windows that can
// be hit by J(t) = floor( Q*t^2 + B*g(t) + C ) for real Q,B,C and a few choices of g: none, |t|, t, t^4, t^3.
// Solved as a 2- or 3-parameter LP by scanning the non-trivial parameters on a grid finer than the window
// slack and sweeping the free additive constant exactly.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cmath>
static i64 JL[1024],JH[1024]; static int OK[1024];
static int best_C(const std::vector<std::pair<long double,int>>& evin){ return 0; }
int main(){ FILE*f=fopen("wf_jwin.txt","r"); int t;long long a,b;int o;
  while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OK[t+512]=o;} fclose(f);
  const char* gn[5]={"none","|t|","t","t^4/2^18","t^3/2^9"};
  long double Q0=(long double)KC/131072.0L;
  for(int gi=0;gi<5;gi++){
    int best=-1; long double bQ=0,bB=0;
    int nB = gi==0?1:401;
    #pragma omp parallel
    { int lbest=-1; long double lQ=0,lB=0;
      #pragma omp for schedule(dynamic)
      for(int qi=-6000;qi<=6000;qi++){ long double Q=Q0+qi*2e-6L;
        for(int bi=0;bi<nB;bi++){ long double B = gi==0?0.0L : (bi-nB/2)*0.02L;
          std::vector<std::pair<long double,int>> ev; ev.reserve(2044);
          for(int tt=-512;tt<=511;tt++){ if(!OK[tt+512])continue;
            long double g; long double ft=tt;
            switch(gi){ case 0: g=0; break; case 1: g=fabsl(ft); break; case 2: g=ft; break;
              case 3: g=ft*ft*ft*ft/262144.0L; break; default: g=ft*ft*ft/512.0L; }
            long double base=Q*ft*ft+B*g;
            ev.push_back({(long double)JL[tt+512]-base,1}); ev.push_back({(long double)JH[tt+512]+1-base,-1}); }
          std::sort(ev.begin(),ev.end()); int cur=0,mx=0; for(size_t i=0;i<ev.size();i++){cur+=ev[i].second; if(cur>mx)mx=cur;}
          if(mx>lbest){lbest=mx;lQ=Q;lB=B;} } }
      #pragma omp critical
      { if(lbest>best){best=lbest;bQ=lQ;bB=lB;} } }
    printf("J = floor(Q*t^2 + B*%-9s + C):  best %4d / 1022   Q=%.7Lf (KC/2^17=%.7Lf)  B=%.4Lf\n",gn[gi],best,bQ,Q0,bB); }
  return 0; }
