// wf_d.cpp -- sweep the FINAL multiply's configuration (operand roles, latch scale 2^g, rounding constant)
// and for each one measure (a) the free-latch violation floor over nodes 1..31, (b) feasibility,
// (c) pinned count / width histogram of the resulting latch-interval dataset.
// Free-latch floor = sum over t of (31 - max number of node instances whose interval a single latch value can satisfy).
#include "base2.h"
#include <cstring>
#include <map>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static int ROLE, KF;                                  // ROLE 0: rec=J, mul=S ; 1: rec=S, mul=J
static inline i64 T2of(i64 J,i64 S,int u){ i64 P = ROLE? arr(S,J) : arr(J,S); return sm(P + (KF? K:0), u); }
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
  // precompute the per-node V-base and window so the sweep is cheap
  static i64 t2lo[32][1024], t2hi[32][1024];
  for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
      i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
      i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
      i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
      t2lo[node][t+512]=base-hi; t2hi[node][t+512]=base-lo; } }
  printf("role  g  Kf   Jmax        monoviol  freefloor  feasible  pinned  medianwidth\n");
  for(ROLE=0;ROLE<2;ROLE++) for(int g=-2;g<=4;g++) for(KF=1;KF>=0;KF--){
    i64 JMAX = (g>=0)? (12000000ll<<g) : (12000000ll>>(-g));
    if(ROLE==0 && JMAX >= (1ll<<27)) { printf("%4d %2d %3d   %10lld   (rec field overflow, skipped)\n",ROLE,g,KF,(long long)JMAX); continue; }
    // monotonicity check on a coarse+fine grid (full scan is too slow for the big ranges; step 1 up to 2^22 then step 1)
    int mono=0;
    for(int n=1;n<=16;n++){ int u=U2TAB[n]+g; if(u<0){mono=-1;break;} i64 prev=T2of(0,TB[n].S,u);
      for(i64 v=1;v<=JMAX;v++){ i64 c=T2of(v,TB[n].S,u); if(c<prev)mono++; prev=c; } }
    if(mono!=0){ printf("%4d %2d %3d   %10lld   %8d  (non-monotone, intervals unreliable)\n",ROLE,g,KF,(long long)JMAX,mono); continue; }
    static i64 NL[32][1024],NH[32][1024];
    for(int node=1;node<=31;node++){ int n=node>=17?32-node:node; int u=U2TAB[n]+g;
      for(int t=0;t<1024;t++){ i64 lo=t2lo[node][t],hi=t2hi[node][t];
        i64 a1=0,b1=JMAX+1; while(a1<b1){i64 m=a1+(b1-a1)/2; if(T2of(m,TB[n].S,u)>=lo)b1=m;else a1=m+1;}
        i64 a2=-1,b2=JMAX; while(a2<b2){i64 m=a2+(b2-a2+1)/2; if(T2of(m,TB[n].S,u)<=hi)a2=m;else b2=m-1;}
        NL[node][t]=a1;NH[node][t]=a2; } }
    long long freefloor=0; int feas=0,pinned=0; std::vector<i64> widths;
    for(int t=0;t<1024;t++){
      // sweep endpoints for max coverage
      std::vector<std::pair<i64,int>> ev;
      for(int node=1;node<=31;node++){ if(NL[node][t]>NH[node][t]) continue; ev.push_back({NL[node][t],+1}); ev.push_back({NH[node][t]+1,-1}); }
      int nvalid=0; for(int node=1;node<=31;node++) if(NL[node][t]<=NH[node][t]) nvalid++;
      std::sort(ev.begin(),ev.end()); int cur=0,best=0; for(size_t i=0;i<ev.size();i++){ cur+=ev[i].second; if(cur>best)best=cur; }
      freefloor += 31-best;
      i64 gl=0,gh=JMAX; for(int node=1;node<=31;node++){ if(NL[node][t]>gl)gl=NL[node][t]; if(NH[node][t]<gh)gh=NH[node][t]; }
      if(gl<=gh){feas++; if(gl==gh)pinned++; widths.push_back(gh-gl);} }
    std::sort(widths.begin(),widths.end());
    printf("%4d %2d %3d   %10lld   %8d  %9lld  %8d  %6d  %11lld\n",ROLE,g,KF,(long long)JMAX,mono,freefloor,feas,pinned,
      widths.empty()?-1:(long long)widths[widths.size()/2]);
    if(ROLE==0&&g==0&&KF==1){ FILE*f=fopen("wf_jwin_base.txt","w"); for(int t=0;t<1024;t++){ i64 gl=0,gh=JMAX;
        for(int node=1;node<=31;node++){ if(NL[node][t]>gl)gl=NL[node][t]; if(NH[node][t]<gh)gh=NH[node][t]; }
        fprintf(f,"%d %lld %lld %d\n",t-512,(long long)gl,(long long)gh,gl<=gh?1:0);} fclose(f);} }
  return 0; }
