// f2_chain_full.cpp -- score the chain configurations that pass the three pinned offsets against ALL 1024
// offsets, EXACTLY: r1 and r2 are swept over their full periods (not sampled) by the breakpoint/difference-
// array method, so the number printed is the maximum any (r1,r2) can achieve for that configuration.
//   I1 = ((ARR1 + r1) >> s1) + p ,  J = ((ARR2 + r2) >> s2) + q
// The 29 three-offset survivors all need p in {-2..-4} and q in {-7..-11}: a constant SUBTRACTED at the
// latches.  That is the degree of freedom b26/b38 did not have (they swept each constant inside one period).
#include "f2_core.h"
#include <algorithm>
#include <omp.h>
static Fsca F;
struct Cfg{int g,s1,s2,r1,r2;i64 kc;int p,q;};
int main(){ if(!load_fsca(F))return 1; build_tables(); FIN_F=&F; Fin f{0,0,1,0}; FinCtx C; fin_build(f,C,2);
  std::vector<Cfg> cf;
  int G[5]={8,9,10,10,11}, S1[5]={15,17,18,19,19}, S2[5]={16,16,17,16,18};
  for(int i=0;i<5;i++) for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(i64 k=KC;k<=KC+4;k++)
    for(int p=-6;p<=2;p++) for(int q=-16;q<=4;q++) cf.push_back({G[i],S1[i],S2[i],r1,r2,k,p,q});
  printf("configurations to score exactly over all 1024 offsets: %zu\n",cf.size());
  std::vector<int> best(cf.size(),0),bestp(cf.size(),0); std::vector<long long> bestr1(cf.size()),bestr2(cf.size()),bestpr1(cf.size()),bestpr2(cf.size());
  #pragma omp parallel for schedule(dynamic)
  for(size_t ci=0;ci<cf.size();ci++){ Cfg c=cf[ci];
    std::vector<i64> ARR1(1024), thr(1024), n1(1024), dx(1024);
    for(int t=-512;t<=511;t++){ int i=t+512; dx[i]=(i64)t<<c.g;
      ARR1[i]= c.r1? arr(c.kc,dx[i]) : arr(dx[i],c.kc);
      n1[i]=ARR1[i]>>c.s1; thr[i]=(1ll<<c.s1)-(ARR1[i]-(n1[i]<<c.s1)); }
    std::vector<i64> cuts; cuts.push_back(0); for(int i=0;i<1024;i++) if(thr[i]<(1ll<<c.s1)) cuts.push_back(thr[i]);
    std::sort(cuts.begin(),cuts.end()); cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
    std::vector<int> diff((size_t)(1ll<<c.s2)+2);
    int bb=0; long long br1=0,br2=0; int bbp=0; long long bpr1=0,bpr2=0;
    for(i64 r1: cuts){ std::fill(diff.begin(),diff.end(),0);
      // the r2 that additionally HONOUR the three pinned offsets
      i64 pl=0, ph=(1ll<<c.s2)-1; bool pok=true;
      for(int t: {17,342,-342}){ int i=t+512; i64 I1=n1[i]+(r1>=thr[i]?1:0)+c.p;
        i64 A2= c.r2? arr(dx[i],I1) : arr(I1,dx[i]);
        i64 lo=((C.JL[i]-c.q)<<c.s2)-A2, hi=((C.JH[i]-c.q+1)<<c.s2)-1-A2;
        if(lo<0)lo=0; if(hi>(1ll<<c.s2)-1)hi=(1ll<<c.s2)-1; if(lo>hi){pok=false;break;}
        pl=std::max(pl,lo); ph=std::min(ph,hi); }
      if(pl>ph) pok=false;
      for(int t=-512;t<=511;t++){ int i=t+512; if(!t) continue; if(C.JL[i]>C.JH[i]) continue;
        i64 I1=n1[i]+(r1>=thr[i]?1:0)+c.p; if(I1<=-(1ll<<26)||I1>=(1ll<<26)) continue;
        i64 A2= c.r2? arr(dx[i],I1) : arr(I1,dx[i]);
        i64 lo=((C.JL[i]-c.q)<<c.s2)-A2, hi=((C.JH[i]-c.q+1)<<c.s2)-1-A2;
        if(lo<0) lo=0; if(hi>(1ll<<c.s2)-1) hi=(1ll<<c.s2)-1; if(lo>hi) continue;
        diff[lo]++; diff[hi+1]--; }
      int run=0; for(i64 v=0;v<(1ll<<c.s2);v++){ run+=diff[v]; if(run>bb){bb=run;br1=r1;br2=v;}
        if(pok&&v>=pl&&v<=ph&&run>bbp){bbp=run;bpr1=r1;bpr2=v;} } }
    best[ci]=bb; bestr1[ci]=br1; bestr2[ci]=br2; bestp[ci]=bbp; bestpr1[ci]=bpr1; bestpr2[ci]=bpr2; }
  std::vector<size_t> idx(cf.size()); for(size_t i=0;i<idx.size();i++) idx[i]=i;
  std::sort(idx.begin(),idx.end(),[&](size_t a,size_t b){return best[a]>best[b];});
  printf("top 15 by offsets satisfied (out of 1024), r1/r2 unconstrained:\n");
  for(int i=0;i<15;i++){ size_t j=idx[i]; Cfg c=cf[j];
    printf("  %4d/1024  g=%2d s1=%2d s2=%2d role1=%d role2=%d KC=%lld p=%3d q=%3d  r1=%lld r2=%lld\n",
      best[j],c.g,c.s1,c.s2,c.r1,c.r2,(long long)c.kc,c.p,c.q,bestr1[j],bestr2[j]); }
  std::sort(idx.begin(),idx.end(),[&](size_t a,size_t b){return bestp[a]>bestp[b];});
  printf("\ntop 15 among configurations that HONOUR t=17, 342 and -342 simultaneously:\n");
  for(int i=0;i<15;i++){ size_t j=idx[i]; Cfg c=cf[j]; if(!bestp[j]) break;
    printf("  %4d/1024  g=%2d s1=%2d s2=%2d role1=%d role2=%d KC=%lld p=%3d q=%3d  r1=%lld r2=%lld\n",
      bestp[j],c.g,c.s1,c.s2,c.r1,c.r2,(long long)c.kc,c.p,c.q,bestpr1[j],bestpr2[j]); }
  int nany=0; for(size_t j=0;j<cf.size();j++) if(bestp[j]) nany++;
  printf("\nconfigurations (of %zu) that can honour all three pinned offsets at once: %d\n",cf.size(),nany);
  return 0; }
