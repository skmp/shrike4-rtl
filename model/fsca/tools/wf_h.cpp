// wf_h.cpp -- how good can the square chain possibly be?
// Step 1: invert the SECOND multiply exactly -> the allowed interval for the first latch I1 at every t.
//         (reports how many t are unreachable by ANY I1, which caps the score for that second stage)
// Step 2: ask for the best FIRST stage of the form  I1(t) = floor( a*t + b_class )  where the class is
//         (t mod 8, sign t): 16 free offsets, one shared slope.  This subsumes every possible array
//         row-truncation loss and rounding constant in the first multiply, so its score is an upper bound
//         on the whole "constant x dx -> latch -> x dx -> latch" family.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cmath>
static i64 JL[1024],JH[1024]; static int OK[1024];
static int S2,ROLE; static i64 K2;
static inline i64 Jof(i64 I1,i64 dx){ i64 P = ROLE? arr(dx,I1) : arr(I1,dx); return (P+K2)>>S2; }
int main(){ FILE*f=fopen("wf_jwin.txt","r"); int t;long long a,b;int o;
  while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OK[t+512]=o;} fclose(f);
  printf(" s2   K2          role  reachable  best16(cap)  bestslope     unreach_t(first 12)\n");
  for(S2=16;S2<=24;S2++) for(int k2=0;k2<4;k2++) for(ROLE=0;ROLE<2;ROLE++){
    K2 = k2==0?0 : k2==1?(1ll<<19) : k2==2?(1ll<<(S2-1)) : (1ll<<S2);
    std::vector<i64> IL(1024),IH(1024); std::vector<char> ok2(1024,0); int reach=0; std::vector<int> un;
    for(int tt=-512;tt<=511;tt++){ if(!OK[tt+512]||tt==0) continue; i64 dx=(i64)tt<<13;
      long double c=(long double)KC*tt*powl(2.0L,(long double)S2-30.0L); i64 c0=(i64)llroundl(c);
      i64 W=60000,lo,hi;
      if(tt>0){ i64 a1=c0-W,b1=c0+W+1; while(a1<b1){i64 m=a1+(b1-a1)/2; if(Jof(m,dx)>=JL[tt+512])b1=m;else a1=m+1;} lo=a1;
                i64 a2=c0-W-1,b2=c0+W; while(a2<b2){i64 m=a2+(b2-a2+1)/2; if(Jof(m,dx)<=JH[tt+512])a2=m;else b2=m-1;} hi=a2; }
      else    { i64 a1=c0-W,b1=c0+W+1; while(a1<b1){i64 m=a1+(b1-a1)/2; if(Jof(m,dx)<=JH[tt+512])b1=m;else a1=m+1;} lo=a1;
                i64 a2=c0-W-1,b2=c0+W; while(a2<b2){i64 m=a2+(b2-a2+1)/2; if(Jof(m,dx)>=JL[tt+512])a2=m;else b2=m-1;} hi=a2; }
      int good=0;
      if(lo<=hi){ good=1; for(i64 v=lo;v<=hi&&v<=lo+4096;v++){ i64 J=Jof(v,dx); if(J<JL[tt+512]||J>JH[tt+512]){good=0;break;} } }
      if(good){ IL[tt+512]=lo; IH[tt+512]=hi; ok2[tt+512]=1; reach++; } else un.push_back(tt); }
    // Step 2: 16 classes
    std::vector<int> cls[16];
    for(int tt=-512;tt<=511;tt++){ if(!ok2[tt+512])continue; int r=((tt%8)+8)%8; int c=r + (tt<0?8:0); cls[c].push_back(tt); }
    long double a0=(long double)KC*powl(2.0L,(long double)S2-30.0L);
    int best=-1; long double bslope=a0;
    for(int pass=0;pass<2;pass++){
      long double lo = pass? bslope-0.002L : a0-4.0L, hi = pass? bslope+0.002L : a0+4.0L;
      int steps = pass? 4000 : 40000;
      for(int i=0;i<=steps;i++){ long double aa = lo + (hi-lo)*i/steps; int tot=0;
        for(int c=0;c<16;c++){ if(cls[c].empty())continue; std::vector<std::pair<long double,int>> ev;
          for(int tt:cls[c]){ ev.push_back({IL[tt+512]-aa*tt,1}); ev.push_back({IH[tt+512]+1-aa*tt,-1}); }
          std::sort(ev.begin(),ev.end()); int cur=0,mx=0; for(size_t j=0;j<ev.size();j++){cur+=ev[j].second; if(cur>mx)mx=cur;} tot+=mx; }
        if(tot>best){best=tot;bslope=aa;} } }
    printf("%3d  %-11lld  %d   %5d      %5d      %12.6Lf   ",S2,(long long)K2,ROLE,reach,best,bslope);
    for(size_t i=0;i<un.size()&&i<12;i++)printf("%d ",un[i]); printf("\n"); }
  return 0; }
