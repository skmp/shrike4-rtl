// wf_p.cpp -- the FINAL square multiply's alignment.  The J windows are entirely determined by where the
// truncation boundaries of T2 = align(arr(J,S), u2) sit, so a wrong rounding constant or alignment rule fakes
// an asymmetry.  Sweep: rounding constant, truncation rule, operand roles, coefficient provenance.
// The decisive number is SYMCAP: the best score any rule with J(t)=J(-t) could reach.  symcap = 1022 would
// mean the square path needs no sign asymmetry at all.
#include "base2.h"
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 SV[17]; static int UV[17]; static int ROLE,KMODE;
static inline i64 T2of(i64 J,int n){ int u=UV[n]; i64 P = ROLE? arr(SV[n],J) : arr(J,SV[n]);
  i64 Kr = KMODE==0? (1ll<<19) : KMODE==1? 0 : KMODE==2? (1ll<<(u-1)) : ((1ll<<19)+(1ll<<(u-1)));
  return (P+Kr)>>u; }
static i64 t2lo[32][1024],t2hi[32][1024];
static i64 NL[32][1024],NH[32][1024];
int main(){ if(!load_fsca(F))return 1; build_tables();
  int kk[17]; for(int n=0;n<=16;n++) kk[n]=TB[n].u2-30;
  for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
      i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
      i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
      i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
      t2lo[node][t+512]=base-hi; t2hi[node][t+512]=base-lo; } }
  printf("Sprov Kmode role dC0  feas pinned freefloor SYMCAP medw chain single\n");
  for(int sp=0;sp<2;sp++) for(KMODE=0;KMODE<4;KMODE++) for(ROLE=0;ROLE<2;ROLE++) for(int dc0=0;dc0<2;dc0++){
    for(int n=1;n<=16;n++){ int k=kk[n]; i64 c0=C0TAB[n];
      if(sp==0){ SV[n]=TB[n].S; UV[n]=30+k; } else { SV[n]=(3-k>=0)?(c0>>(3-k)):(c0<<(k-3)); UV[n]=30+k; } }
    i64 JMAX=12000000; i64 off = dc0? 1 : 0;      // dc0: shift every C0 by +1 to test C0 sensitivity
    #pragma omp parallel for schedule(static)
    for(int t=0;t<1024;t++)
      for(int node=1;node<=31;node++){ int n=node>=17?32-node:node;
        i64 lo=t2lo[node][t]+off,hi=t2hi[node][t]+off;
        i64 a1=0,b1=JMAX+1; while(a1<b1){i64 m=a1+(b1-a1)/2; if(T2of(m,n)>=lo)b1=m;else a1=m+1;}
        i64 a2=-1,b2=JMAX; while(a2<b2){i64 m=a2+(b2-a2+1)/2; if(T2of(m,n)<=hi)a2=m;else b2=m-1;}
        NL[node][t]=a1;NH[node][t]=a2; }
    long long ff=0; int feas=0,pinned=0; std::vector<i64> GL(1024),GH(1024); std::vector<char> ok(1024,0); std::vector<i64> w;
    for(int t=0;t<1024;t++){ std::vector<std::pair<i64,int>> ev;
      for(int node=1;node<=31;node++){ if(NL[node][t]>NH[node][t])continue; ev.push_back({NL[node][t],1}); ev.push_back({NH[node][t]+1,-1}); }
      std::sort(ev.begin(),ev.end()); int cur=0,bst=0; for(size_t i=0;i<ev.size();i++){cur+=ev[i].second; if(cur>bst)bst=cur;}
      ff += 31-bst;
      i64 gl=0,gh=JMAX; for(int node=1;node<=31;node++){ if(NL[node][t]>gl)gl=NL[node][t]; if(NH[node][t]<gh)gh=NH[node][t]; }
      GL[t]=gl;GH[t]=gh; if(gl<=gh){feas++;ok[t]=1;if(gl==gh)pinned++;w.push_back(gh-gl);} }
    std::sort(w.begin(),w.end());
    int symcap=0; for(int at=0;at<=511;at++){ int ip=at+512,im=(at?-at:0)+512;
      if(at==0){symcap+=ok[512]?1:0;continue;}
      if(ok[ip]&&ok[im]) symcap += (std::max(GL[ip],GL[im])<=std::min(GH[ip],GH[im]))?2:1; else symcap += (ok[ip]||ok[im])?1:0; }
    if(ok[0])symcap++;
    int bchain=0,bsingle=0;
    for(int s1=21;s1<=27;s1++){ int s2=43-s1;
      for(int dk=-4;dk<=4;dk++) for(int k1=0;k1<3;k1++) for(int k2=0;k2<3;k2++) for(int r2=0;r2<2;r2++){
        i64 Kq=KC+dk,K1=k1==0?0:k1==1?(1ll<<19):(1ll<<(s1-1)),K2=k2==0?0:k2==1?(1ll<<19):(1ll<<(s2-1)); int sc=0;
        for(int t=-512;t<=511;t++){ if(!ok[t+512])continue; i64 dx=(i64)t<<13;
          i64 I1=(arr(Kq,dx)+K1)>>s1; if(I1>=(1ll<<27)||I1<=-(1ll<<27)){sc=0;break;}
          i64 J=((r2?arr(dx,I1):arr(I1,dx))+K2)>>s2; if(J>=GL[t+512]&&J<=GH[t+512])sc++; }
        if(sc>bchain)bchain=sc; } }
    for(int g=0;g<=6;g++) for(int dk=-10;dk<=10;dk++) for(int kr=0;kr<8;kr++){
      i64 Kq=KC+dk; int sh=17+g; i64 Kr=kr==0?0:(1ll<<(kr+13)); if(kr&&Kr>=(1ll<<sh))continue; int sc=0;
      for(int t=-512;t<=511;t++){ if(!ok[t+512])continue; i64 J=(arr(Kq,((i64)t*t)<<g)+Kr)>>sh;
        if(J>=GL[t+512]&&J<=GH[t+512])sc++; }
      if(sc>bsingle)bsingle=sc; }
    printf("%5d %5d %4d %3d  %4d %6d %9lld %6d %4lld %5d %6d\n",sp,KMODE,ROLE,dc0,feas,pinned,ff,symcap,
      w.empty()?-1:(long long)w[w.size()/2],bchain,bsingle); }
  return 0; }
