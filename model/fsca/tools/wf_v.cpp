// wf_v.cpp -- search for the best square-latch rule SUBJECT TO the guard: nodes 0,1,2,16 (and their mirrors
// 30,31) must stay inside their windows at all 1024 offsets.  Any rule that breaks the guard is a fit, not a
// datapath.  Reports the guard-feasible ceiling too: the best score any J(t) could reach while respecting the
// guard, which separates "the rule is wrong" from "the guard forbids it".
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
static i64 JL[1024],JH[1024]; static int OKW[1024];
static i64 GLo[1024],GHi[1024];          // guard window from nodes 1,2,16,30,31
int main(){ if(!load_fsca(F))return 1; build_tables();
  { FILE*f=fopen("wf_jwin.txt","r"); int t;long long a,b;int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKW[t+512]=o;} fclose(f); }
  int guardnodes[5]={1,2,16,30,31};
  for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000;
    for(int gi=0;gi<5;gi++){ int node=guardnodes[gi]; int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
      i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
      i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
      i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
      i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n];
      i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
      i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
      if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
    GLo[t+512]=gl;GHi[t+512]=gh; }
  int gempty=0; for(int t=0;t<1024;t++) if(GLo[t]>GHi[t])gempty++;
  // ceiling: best score with a free J(t) that also respects the guard
  int ceil_=0; for(int t=0;t<1024;t++){ if(!OKW[t])continue;
    i64 lo=std::max(GLo[t],JL[t]), hi=std::min(GHi[t],JH[t]); if(lo<=hi)ceil_++; }
  printf("guard windows empty at %d of 1024 t; guard-respecting ceiling on the latch score: %d / 1022\n",gempty,ceil_);
  { std::vector<i64> w; for(int t=0;t<1024;t++) if(GLo[t]<=GHi[t]) w.push_back(GHi[t]-GLo[t]);
    std::sort(w.begin(),w.end()); printf("guard window width: median %lld, min %lld, max %lld\n",(long long)w[w.size()/2],(long long)w.front(),(long long)w.back()); }
  auto eval=[&](const std::vector<i64>&J,int&okguard,int&score){ okguard=1;score=0;
    for(int t=0;t<1024;t++){ if(J[t]<GLo[t]||J[t]>GHi[t]) okguard=0;
      if(OKW[t]&&J[t]>=JL[t]&&J[t]<=JH[t])score++; } };
  int best=-1; char bd[256]="none";
  std::vector<i64> J(1024);
  // A. chain family, guard-constrained
  for(int s1=18;s1<=30;s1++) for(int sum=43;sum<=46;sum++){ int s2=sum-s1; if(s2<4||s2>34)continue; i64 kb=(i64)KC<<(sum-43); if(kb>=(1ll<<27))continue;
    for(i64 dk=-8;dk<=8;dk++) for(int k1=0;k1<4;k1++) for(int k2=0;k2<4;k2++) for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++){
      i64 Kq=kb+dk; i64 K1=k1==0?0:k1==1?(1ll<<19):k1==2?(1ll<<(s1-1)):(1ll<<s1);
      i64 K2=k2==0?0:k2==1?(1ll<<19):k2==2?(1ll<<(s2-1)):(1ll<<s2); int ovf=0;
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=((r1?arr(dx,Kq):arr(Kq,dx))+K1)>>s1;
        if(I1>=(1ll<<27)||I1<=-(1ll<<27)){ovf=1;break;} J[t+512]=((r2?arr(dx,I1):arr(I1,dx))+K2)>>s2; }
      if(ovf)continue; int og,sc; eval(J,og,sc);
      if(og&&sc>best){best=sc;snprintf(bd,256,"chain s1=%d s2=%d Kq=%lld K1=%lld K2=%lld r1=%d r2=%d",s1,s2,(long long)Kq,(long long)K1,(long long)K2,r1,r2);} } }
  // B. single multiply on the exact t^2 latch, guard-constrained, with an optional odd term
  for(int g=0;g<=4;g++) for(int role=0;role<2;role++) for(i64 dk=-8;dk<=8;dk++) for(int kr=0;kr<12;kr++) for(int beta=-2000;beta<=2000;beta+=4){
    i64 Kq=KC+dk; int sh=17+g; i64 Kr=kr==0?0:(1ll<<(kr+8)); if(kr&&Kr>=(1ll<<sh))continue;
    for(int t=-512;t<=511;t++){ i64 sq=((i64)t*t)<<g; J[t+512]=((role?arr(sq,Kq):arr(Kq,sq))+Kr+(i64)beta*t)>>sh; }
    int og,sc; eval(J,og,sc);
    if(og&&sc>best){best=sc;snprintf(bd,256,"single: (arr%s(Kq=%lld,t^2<<%d)+%lld+%d*t)>>%d",role?"(t^2,Kq)":"(Kq,t^2)",(long long)Kq,g,(long long)Kr,beta,sh);} }
  // C. affine-in-t^2 floor at the 2^24 scale, guard-constrained
  { i64 base=(i64)KC<<7;
    #pragma omp parallel
    { int lb=-1; char ld[256]={0}; std::vector<i64> LJ(1024);
      #pragma omp for schedule(dynamic)
      for(int qi=-400;qi<=400;qi++) for(int bi=-100000;bi<=100000;bi+=250) for(int ci=-20;ci<=20;ci++){
        i64 Q=base+qi,B=bi,C=(i64)ci*(1ll<<20);
        for(int t=-512;t<=511;t++) LJ[t+512]=(Q*(i64)t*t+B*t+C)>>24;
        int og=1,sc=0; for(int t=0;t<1024;t++){ if(LJ[t]<GLo[t]||LJ[t]>GHi[t]){og=0;break;} }
        if(!og)continue; for(int t=0;t<1024;t++) if(OKW[t]&&LJ[t]>=JL[t]&&LJ[t]<=JH[t])sc++;
        if(sc>lb){lb=sc;snprintf(ld,256,"floor((%lld*t^2 %+lld*t %+lld)/2^24)",(long long)Q,(long long)B,(long long)C);} }
      #pragma omp critical
      { if(lb>best){best=lb;strcpy(bd,ld);} } } }
  printf("\nbest GUARD-RESPECTING latch rule: %d / 1022   %s\n",best,bd);
  return 0; }
