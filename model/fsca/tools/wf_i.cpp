// wf_i.cpp -- the FINAL multiply's configuration: operand roles (rec = latch vs rec = table sine, the latter
// being what the linear and cubic terms do), latch scale 2^g with final shift u2+g, rounding constant.
// For each: regenerate the latch-interval dataset, report feasibility, the free-latch violation floor over the
// 31 node instances, the pinned count, and the best score of a chain sweep on that dataset.
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
static int ROLE,KFLAG;
static inline i64 T2of(i64 J,i64 S,int u){ i64 P = ROLE? arr(S,J) : arr(J,S); return sm(P + (KFLAG?K:0), u); }
static i64 t2lo[32][1024],t2hi[32][1024];
static i64 NL[32][1024],NH[32][1024];
int main(){ if(!load_fsca(F))return 1; build_tables();
  for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
      i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
      i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
      i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
      t2lo[node][t+512]=base-hi; t2hi[node][t+512]=base-lo; } }
  printf("role  g  K   feasible  pinned  freefloor  medw   bestchain\n");
  for(ROLE=0;ROLE<2;ROLE++) for(int g=-3;g<=2;g++) for(KFLAG=1;KFLAG>=0;KFLAG--){
    i64 JMAX = (g>=0)? (12000000ll<<g) : (12000000ll>>(-g));
    if(ROLE==0 && JMAX>=(1ll<<27)) continue;
    #pragma omp parallel for schedule(static)
    for(int t=0;t<1024;t++){
      for(int node=1;node<=31;node++){ int n=node>=17?32-node:node; int u=U2TAB[n]+g; i64 S=TB[n].S;
        i64 lo=t2lo[node][t],hi=t2hi[node][t];
        i64 a1=0,b1=JMAX+1; while(a1<b1){i64 m=a1+(b1-a1)/2; if(T2of(m,S,u)>=lo)b1=m;else a1=m+1;}
        i64 a2=-1,b2=JMAX; while(a2<b2){i64 m=a2+(b2-a2+1)/2; if(T2of(m,S,u)<=hi)a2=m;else b2=m-1;}
        NL[node][t]=a1;NH[node][t]=a2; } }
    long long freefloor=0; int feas=0,pinned=0; std::vector<i64> w; std::vector<i64> GL(1024),GH(1024); std::vector<char> ok(1024,0);
    for(int t=0;t<1024;t++){ std::vector<std::pair<i64,int>> ev;
      for(int node=1;node<=31;node++){ if(NL[node][t]>NH[node][t])continue; ev.push_back({NL[node][t],1}); ev.push_back({NH[node][t]+1,-1}); }
      std::sort(ev.begin(),ev.end()); int cur=0,best=0; for(size_t i=0;i<ev.size();i++){cur+=ev[i].second; if(cur>best)best=cur;}
      freefloor += 31-best;
      i64 gl=0,gh=JMAX; for(int node=1;node<=31;node++){ if(NL[node][t]>gl)gl=NL[node][t]; if(NH[node][t]<gh)gh=NH[node][t]; }
      GL[t]=gl;GH[t]=gh; if(gl<=gh){feas++; ok[t]=1; if(gl==gh)pinned++; w.push_back(gh-gl);} }
    std::sort(w.begin(),w.end());
    // quick chain sweep on this dataset: I1 = (arr(Kq,dx)+K1)>>s1 ; J = (arr(I1,dx)+K2)>>s2, s1+s2 = 43-g, Kq=KC
    int bestchain=0;
    for(int s1=18;s1<=32;s1++){ int s2=43-g-s1; if(s2<6||s2>34)continue;
      for(int dk=-4;dk<=4;dk++) for(int k1=0;k1<3;k1++) for(int k2=0;k2<3;k2++) for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++){
        i64 Kq=KC+dk; i64 K1=k1==0?0:k1==1?(1ll<<19):(1ll<<(s1-1)); i64 K2=k2==0?0:k2==1?(1ll<<19):(1ll<<(s2-1));
        int sc=0; for(int t=-512;t<=511;t++){ if(!ok[t+512])continue; i64 dx=(i64)t<<13;
          i64 P1=r1?arr(dx,Kq):arr(Kq,dx); i64 I1=(P1+K1)>>s1; if(I1>=(1ll<<27)||I1<=-(1ll<<27)){sc=0;break;}
          i64 P2=r2?arr(dx,I1):arr(I1,dx); i64 J=(P2+K2)>>s2;
          if(J>=GL[t+512]&&J<=GH[t+512])sc++; }
        if(sc>bestchain)bestchain=sc; } }
    printf("%4d %2d %2d   %6d  %6d  %9lld  %5lld  %7d\n",ROLE,g,KFLAG,feas,pinned,freefloor,w.empty()?-1:w[w.size()/2],bestchain); }
  return 0; }
