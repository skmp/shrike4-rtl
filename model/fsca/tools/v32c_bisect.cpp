// v32c_bisect.cpp -- where does 694 come from?  Per-offset comparison of the bisection window
// (w3_common.h / b39 method) against the honest window, at ND=13, plus a direct exhaustive
// feasibility test at the offsets where they disagree.
#include "v32_common.h"
#include <vector>
static const i64 LIMW = 12000000ll<<2;
int main(){ if(!load_fsca(FD)){puts("no dump");return 1;} build_tables(); load_jwin();
  const int nd=13; const i64 CUT=1ll<<25;
  int disagree=0, disagree_small=0, minabs=1<<30, maxabs=0;
  int bis_wrong_infeas=0, bis_wrong_feas=0;
  std::vector<int> bad;
  i64 gl[1024],gh[1024];
  for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 L=0,H=LIMW; i64 A3=A3_of(t,dx,nd);
      for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
          i64 s=flip?-1:1; i64 T1=T1_of(n,dx,nd),T3=T3_of(n,A3,nd);
          i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
          i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n]+2;
          i64 a1=0,b1=LIMW+1; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arrN(m,TB[n].S,nd)+K,u)>=t2lo)b1=m;else a1=m+1;}
          i64 a2=-1,b2=LIMW;  while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arrN(m,TB[n].S,nd)+K,u)<=t2hi)a2=m;else b2=m-1;}
          if(a1>L)L=a1; if(a2<H)H=a2; }
      gl[t+512]=L; gh[t+512]=H;
      i64 hL=FL[t+512], hH=std::min(FH[t+512],CUT-1);
      if(L!=hL||H!=hH){ disagree++; int a=t<0?-t:t; if(a<minabs)minabs=a; if(a>maxabs)maxabs=a;
          if(FL[t+512]<CUT){ disagree_small++; bad.push_back(t); } } }
  printf("ND=13: bisection window differs from the honest window at %d/1024 offsets, |t| in [%d,%d]\n",
         disagree,minabs,maxabs);
  printf("of those, %d are offsets whose true latch is BELOW 2^25 -- i.e. offsets the array can represent\n",disagree_small);
  // direct exhaustive test at a few of them: is the baseline J really feasible there under ND=13?
  int shown=0, confirmed=0;
  for(int t : bad){ i64 J=J_of(t,nd,18,0); bool ok=true;
      for(int node=1;node<=31&&ok;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
          i64 V=V_of(node,t,J,nd,2); int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
          if(V<lo||V>hi) ok=false; }
      bool bisSaysOk = (J>=gl[t+512]&&J<=gh[t+512]);
      if(ok&&!bisSaysOk){ confirmed++;
          if(shown<6){ printf("  t=%4d J=%lld : ALL 31 nodes reproduce the dump, bisection window [%lld,%lld] says NO\n",
               t,(long long)J,(long long)gl[t+512],(long long)gh[t+512]); shown++; } } }
  printf("offsets where the ND=13 model VERIFIABLY reproduces all 31 nodes but the bisection scored it a miss: %d\n",confirmed);
  // and the honest totals again, recomputed independently by direct simulation (no windows at all)
  int direct=0, feasdirect=0;
  for(int t=-512;t<=511;t++){ i64 J=J_of(t,nd,18,0); bool ok=true;
      for(int node=1;node<=31&&ok;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
          i64 V=V_of(node,t,J,nd,2); int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
          if(V<lo||V>hi) ok=false; }
      if(ok)direct++; (void)feasdirect; }
  printf("\nDIRECT simulation (no window file, no bisection): ND=13 fine chain reproduces all 31 node\n");
  printf("instances at %d/1024 offsets.  ND=14 baseline, same test:",direct);
  int d14=0; for(int t=-512;t<=511;t++){ i64 J=J_of(t,14,18,0); bool ok=true;
      for(int node=1;node<=31&&ok;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
          i64 V=V_of(node,t,J,14,2); int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi); if(V<lo||V>hi)ok=false; }
      if(ok)d14++; }
  printf(" %d/1024\n",d14);
  return 0; }
