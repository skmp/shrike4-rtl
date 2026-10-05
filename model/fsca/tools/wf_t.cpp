// wf_t.cpp -- is the required sign asymmetry real, or an artefact of the 16 stored square-sine fields?
// One LSB of S_n moves node n's required latch by about J/S_n ~ 1.2 units -- exactly the size of the
// asymmetry the data demands.  Greedily fit an integer offset sigma_n (|sigma| <= 3) per table to maximise
// SYMCAP, the best score any rule with J(t) = J(-t) could reach.  If SYMCAP reaches 1022 the square path
// needs no sign asymmetry at all.
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
static i64 t2lo[32][1024],t2hi[32][1024];
static i64 NL[32][1024],NH[32][1024];
static i64 SS[17]; static int sig[17];
static void invert(int node){ int n=node>=17?32-node:node; int u=U2TAB[n]; i64 S=SS[n]; i64 JMAX=12000000;
  for(int t=0;t<1024;t++){ i64 lo=t2lo[node][t],hi=t2hi[node][t];
    i64 a1=0,b1=JMAX+1; while(a1<b1){i64 m=a1+(b1-a1)/2; if(sm(arr(m,S)+K,u)>=lo)b1=m;else a1=m+1;}
    i64 a2=-1,b2=JMAX; while(a2<b2){i64 m=a2+(b2-a2+1)/2; if(sm(arr(m,S)+K,u)<=hi)a2=m;else b2=m-1;}
    NL[node][t]=a1;NH[node][t]=a2; } }
static void metrics(int&feas,long long&ff,int&symcap,std::vector<i64>&GL,std::vector<i64>&GH,std::vector<char>&ok){
  feas=0; ff=0; GL.assign(1024,0);GH.assign(1024,0);ok.assign(1024,0);
  for(int t=0;t<1024;t++){ std::vector<std::pair<i64,int>> ev;
    for(int node=1;node<=31;node++){ if(NL[node][t]>NH[node][t])continue; ev.push_back({NL[node][t],1}); ev.push_back({NH[node][t]+1,-1}); }
    std::sort(ev.begin(),ev.end()); int cur=0,bst=0; for(size_t i=0;i<ev.size();i++){cur+=ev[i].second; if(cur>bst)bst=cur;}
    ff += 31-bst;
    i64 gl=0,gh=12000000; for(int node=1;node<=31;node++){ if(NL[node][t]>gl)gl=NL[node][t]; if(NH[node][t]<gh)gh=NH[node][t]; }
    GL[t]=gl;GH[t]=gh; if(gl<=gh){feas++;ok[t]=1;} }
  symcap=0; for(int at=0;at<=511;at++){ int ip=at+512,im=(at?-at:0)+512;
    if(at==0){symcap+=ok[512]?1:0;continue;}
    if(ok[ip]&&ok[im]) symcap += (std::max(GL[ip],GL[im])<=std::min(GH[ip],GH[im]))?2:1; else symcap += (ok[ip]||ok[im])?1:0; }
  if(ok[0])symcap++; }
int main(){ if(!load_fsca(F))return 1; build_tables();
  for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
      i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
      i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
      i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
      t2lo[node][t+512]=base-hi; t2hi[node][t+512]=base-lo; } }
  for(int n=1;n<=16;n++){ SS[n]=TB[n].S; sig[n]=0; }
  for(int node=1;node<=31;node++) invert(node);
  int feas,symcap; long long ff; std::vector<i64> GL,GH; std::vector<char> ok;
  metrics(feas,ff,symcap,GL,GH,ok);
  printf("start: feasible %d  freefloor %lld  SYMCAP %d\n",feas,ff,symcap);
  int cur=symcap;
  for(int round=0;round<6;round++){ int improved=0;
    for(int n=1;n<=16;n++) for(int d=-1;d<=1;d+=2){ if(sig[n]+d<-3||sig[n]+d>3)continue;
      i64 old=SS[n]; SS[n]=old+d;
      invert(n); if(32-n<=31&&32-n>=17) invert(32-n);
      int f2,s2; long long ff2; std::vector<i64> A,B; std::vector<char> C;
      metrics(f2,ff2,s2,A,B,C);
      if(s2>cur){ cur=s2; sig[n]+=d; improved=1; printf("  table %2d sigma -> %+d : SYMCAP %d (feasible %d, freefloor %lld)\n",n,sig[n],s2,f2,ff2); }
      else { SS[n]=old; invert(n); if(32-n<=31&&32-n>=17) invert(32-n); } }
    if(!improved) break; }
  printf("best SYMCAP with |sigma_n| <= 3 on the 16 square-sine fields: %d / 1022\n",cur);
  printf("sigma:"); for(int n=1;n<=16;n++) printf(" %+d",sig[n]); printf("\n");
  return 0; }
