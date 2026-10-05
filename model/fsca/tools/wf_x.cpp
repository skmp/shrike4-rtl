// wf_x.cpp -- redo the bounds on the FINER latch (g=2: final shift u2+2, latch J2 = 4*J, all 1024 t feasible,
// free-latch floor 0).  Symmetric cap, smooth-quadratic bounds, best explicit rules, and the two-stage chain
// ceiling with 16 free per-class offsets.  Writes wf_jwin_g2.txt.
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
static i64 GL[1024],GH[1024]; static int OKW[1024];
static const int G=2;                       // latch is 2^G finer;  J2 ~ KC2 * t^2 / 2^17 with KC2 = KC<<G
int main(){ if(!load_fsca(F))return 1; build_tables();
  i64 JMAX=12000000ll<<G; i64 KC2=(i64)KC<<G;
  for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=JMAX;
    for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
      i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
      i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
      i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
      i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n]+G;
      i64 a1=0,b1=JMAX+1; while(a1<b1){i64 m=a1+(b1-a1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
      i64 a2=-1,b2=JMAX; while(a2<b2){i64 m=a2+(b2-a2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
      if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
    GL[t+512]=gl;GH[t+512]=gh;OKW[t+512]=gl<=gh; }
  { FILE*f=fopen("wf_jwin_g2.txt","w"); for(int t=-512;t<=511;t++) fprintf(f,"%d %lld %lld %d\n",t,(long long)GL[t+512],(long long)GH[t+512],OKW[t+512]); fclose(f); }
  int feas=0,pin=0; std::vector<i64> w;
  for(int t=0;t<1024;t++) if(OKW[t]){feas++; if(GL[t]==GH[t])pin++; w.push_back(GH[t]-GL[t]);}
  std::sort(w.begin(),w.end());
  int symcap=0; for(int at=0;at<=511;at++){ int ip=at+512,im=(at?-at:0)+512;
    if(at==0){symcap+=OKW[512]?1:0;continue;}
    if(OKW[ip]&&OKW[im]) symcap += (std::max(GL[ip],GL[im])<=std::min(GH[ip],GH[im]))?2:1; else symcap+=(OKW[ip]||OKW[im])?1:0; }
  if(OKW[0])symcap++;
  printf("g=2 dataset: feasible %d/1024, pinned %d, median width %lld, SYMCAP %d\n",feas,pin,(long long)w[w.size()/2],symcap);
  // smooth quadratic, fine B
  { int best=-1; long double bQ=0,bB=0; long double Q0=(long double)KC2/131072.0L;
    #pragma omp parallel
    { int lb=-1; long double lQ=0,lB=0;
      #pragma omp for schedule(dynamic)
      for(int qi=-3000;qi<=3000;qi++){ long double Q=Q0+qi*1.2e-5L;
        for(int bi=-600;bi<=600;bi++){ long double B=bi*8e-5L;
          std::vector<std::pair<long double,int>> ev; ev.reserve(2048);
          for(int t=-512;t<=511;t++){ if(!OKW[t+512])continue; long double base=Q*(long double)t*t+B*t;
            ev.push_back({(long double)GL[t+512]-base,1}); ev.push_back({(long double)GH[t+512]+1-base,-1}); }
          std::sort(ev.begin(),ev.end()); int cur=0,mx=0; for(size_t i=0;i<ev.size();i++){cur+=ev[i].second; if(cur>mx)mx=cur;}
          if(mx>lb){lb=mx;lQ=Q;lB=B;} } }
      #pragma omp critical
      { if(lb>best){best=lb;bQ=lQ;bB=lB;} } }
    printf("  best J2 = floor(Q t^2 + B t + C): %d / %d  Q=%.7Lf (KC2/2^17=%.7Lf) B=%.6Lf\n",best,feas,bQ,Q0,bB); }
  // explicit rules
  { int best=-1; char bd[200]="";
    for(int s1=18;s1<=30;s1++){ int s2=41-s1; if(s2<4||s2>34)continue;
      for(i64 dk=-8;dk<=8;dk++) for(int k1=0;k1<4;k1++) for(int k2=0;k2<4;k2++) for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++){
        i64 Kq=KC+dk; i64 K1=k1==0?0:k1==1?(1ll<<19):k1==2?(1ll<<(s1-1)):(1ll<<s1);
        i64 K2=k2==0?0:k2==1?(1ll<<19):k2==2?(1ll<<(s2-1)):(1ll<<s2); int sc=0,ovf=0;
        for(int t=-512;t<=511;t++){ if(!OKW[t+512])continue; i64 dx=(i64)t<<13;
          i64 I1=((r1?arr(dx,Kq):arr(Kq,dx))+K1)>>s1; if(I1>=(1ll<<27)||I1<=-(1ll<<27)){ovf=1;break;}
          i64 J=((r2?arr(dx,I1):arr(I1,dx))+K2)>>s2; if(J>=GL[t+512]&&J<=GH[t+512])sc++; }
        if(!ovf&&sc>best){best=sc;snprintf(bd,200,"chain s1=%d s2=%d Kq=%lld K1=%lld K2=%lld r1=%d r2=%d",s1,s2,(long long)Kq,(long long)K1,(long long)K2,r1,r2);} } }
    for(int g=0;g<=6;g++) for(int role=0;role<2;role++) for(i64 dk=-32;dk<=32;dk++) for(int kr=0;kr<12;kr++){
      i64 Kq=KC2+dk; int sh=17+g; i64 Kr=kr==0?0:(1ll<<(kr+8)); if(kr&&Kr>=(1ll<<sh))continue; int sc=0;
      for(int t=-512;t<=511;t++){ if(!OKW[t+512])continue; i64 sq=((i64)t*t)<<g;
        i64 J=((role?arr(sq,Kq):arr(Kq,sq))+Kr)>>sh; if(J>=GL[t+512]&&J<=GH[t+512])sc++; }
      if(sc>best){best=sc;snprintf(bd,200,"single (arr(Kq=%lld,t^2<<%d)+%lld)>>%d role=%d",(long long)Kq,g,(long long)Kr,sh,role);} }
    printf("  best explicit rule: %d / %d   %s\n",best,feas,bd); }
  // two-stage chain ceiling, 16 free per-class offsets + free slope, per s2
  for(int S2=18;S2<=23;S2++) for(int k2=0;k2<2;k2++){ i64 K2=k2?(1ll<<19):0;
    std::vector<i64> IL(1024),IH(1024); std::vector<char> ok2(1024,0); int reach=0;
    for(int tt=-512;tt<=511;tt++){ if(!OKW[tt+512]||tt==0)continue; i64 dx=(i64)tt<<13;
      long double c=(long double)KC2*tt*powl(2.0L,(long double)S2-30.0L); i64 c0=(i64)llroundl(c); i64 W=80000,lo,hi;
      auto Jf=[&](i64 v){ return (arr(v,dx)+K2)>>S2; };
      if(tt>0){ i64 a1=c0-W,b1=c0+W+1; while(a1<b1){i64 m=a1+(b1-a1)/2; if(Jf(m)>=GL[tt+512])b1=m;else a1=m+1;} lo=a1;
                i64 a2=c0-W-1,b2=c0+W; while(a2<b2){i64 m=a2+(b2-a2+1)/2; if(Jf(m)<=GH[tt+512])a2=m;else b2=m-1;} hi=a2; }
      else    { i64 a1=c0-W,b1=c0+W+1; while(a1<b1){i64 m=a1+(b1-a1)/2; if(Jf(m)<=GH[tt+512])b1=m;else a1=m+1;} lo=a1;
                i64 a2=c0-W-1,b2=c0+W; while(a2<b2){i64 m=a2+(b2-a2+1)/2; if(Jf(m)>=GL[tt+512])a2=m;else b2=m-1;} hi=a2; }
      int good=0; if(lo<=hi){ good=1; for(i64 v=lo;v<=hi&&v<=lo+2048;v++){ i64 J=Jf(v); if(J<GL[tt+512]||J>GH[tt+512]){good=0;break;} } }
      if(good){IL[tt+512]=lo;IH[tt+512]=hi;ok2[tt+512]=1;reach++;} }
    std::vector<int> cls[16];
    for(int tt=-512;tt<=511;tt++){ if(!ok2[tt+512])continue; int r=((tt%8)+8)%8; cls[r+(tt<0?8:0)].push_back(tt); }
    long double a0=(long double)KC2*powl(2.0L,(long double)S2-30.0L); int best=-1;
    for(int i=-20000;i<=20000;i++){ long double aa=a0+i*4e-4L; int tot=0;
      for(int c=0;c<16;c++){ if(cls[c].empty())continue; std::vector<std::pair<long double,int>> ev;
        for(int tt:cls[c]){ ev.push_back({IL[tt+512]-aa*tt,1}); ev.push_back({IH[tt+512]+1-aa*tt,-1}); }
        std::sort(ev.begin(),ev.end()); int cur=0,mx=0; for(size_t j=0;j<ev.size();j++){cur+=ev[j].second; if(cur>mx)mx=cur;} tot+=mx; }
      if(tot>best)best=tot; }
    printf("  chain ceiling (s2=%d, K2=%lld): reachable %d, best with free slope + 16 class offsets = %d\n",S2,(long long)K2,reach,best); }
  return 0; }
