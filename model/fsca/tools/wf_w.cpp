// wf_w.cpp -- confirmation: the t = +-17 infeasibility and the free-latch floor of 2 are artefacts of the
// square latch's resolution.  For latch scale 2^g (final shift u2+g) report feasibility, the free-latch
// violation floor over the 31 node instances, and the binding node instances at t = +-17.
#include "base2.h"
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 t2lo[32][1024],t2hi[32][1024],NL[32][1024],NH[32][1024];
int main(){ if(!load_fsca(F))return 1; build_tables();
  for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
      i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
      i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
      i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
      t2lo[node][t+512]=base-hi; t2hi[node][t+512]=base-lo; } }
  for(int g=0;g<=3;g++){ i64 JMAX=12000000ll<<g; if(JMAX>=(1ll<<27)){printf("g=%d skipped (latch would exceed the 28-bit Booth field)\n",g);continue;}
    #pragma omp parallel for schedule(static)
    for(int t=0;t<1024;t++)
      for(int node=1;node<=31;node++){ int n=node>=17?32-node:node; int u=U2TAB[n]+g; i64 S=TB[n].S;
        i64 lo=t2lo[node][t],hi=t2hi[node][t];
        i64 a1=0,b1=JMAX+1; while(a1<b1){i64 m=a1+(b1-a1)/2; if(sm(arr(m,S)+K,u)>=lo)b1=m;else a1=m+1;}
        i64 a2=-1,b2=JMAX; while(a2<b2){i64 m=a2+(b2-a2+1)/2; if(sm(arr(m,S)+K,u)<=hi)a2=m;else b2=m-1;}
        NL[node][t]=a1;NH[node][t]=a2; }
    long long ff=0; int feas=0;
    for(int t=0;t<1024;t++){ std::vector<std::pair<i64,int>> ev;
      for(int node=1;node<=31;node++){ if(NL[node][t]>NH[node][t])continue; ev.push_back({NL[node][t],1}); ev.push_back({NH[node][t]+1,-1}); }
      std::sort(ev.begin(),ev.end()); int cur=0,bst=0; for(size_t i=0;i<ev.size();i++){cur+=ev[i].second; if(cur>bst)bst=cur;}
      ff+=31-bst; i64 gl=0,gh=JMAX; for(int node=1;node<=31;node++){ if(NL[node][t]>gl)gl=NL[node][t]; if(NH[node][t]<gh)gh=NH[node][t]; }
      if(gl<=gh)feas++; }
    printf("\ng=%d (final shift u2+%d, latch 2^%d finer): feasible %d/1024, free-latch violation floor %lld\n",g,g,g,feas,ff);
    for(int s=0;s<2;s++){ int t=s?-17:17; i64 gl=0,gh=JMAX; int bl=0,bh=0;
      for(int node=1;node<=31;node++){ if(NL[node][t+512]>gl){gl=NL[node][t+512];bl=node;} if(NH[node][t+512]<gh){gh=NH[node][t+512];bh=node;} }
      printf("   t=%4d: [%lld, %lld] %s  (low from node %d, high from node %d; ideal %.2f)\n",t,(long long)gl,(long long)gh,
        gl<=gh?"OK":"EMPTY",bl,bh,(double)KC*17*17/131072.0*(1<<g)); } }
  return 0; }
