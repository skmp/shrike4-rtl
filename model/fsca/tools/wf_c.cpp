// wf_c.cpp -- per-node J intervals: are the 31 node instances mutually consistent?
// Separates unflipped (nodes 1..16) from flipped (17..31) to test whether the mirror half
// carries the same rule.  Also prints the binding node at interesting t.
#include "base2.h"
#include <cstring>
#include <vector>
#include <cmath>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 NL[32][1024], NH[32][1024];   // per node instance allowed J interval (node 1..31)
int main(){ if(!load_fsca(F))return 1; build_tables();
  for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
      i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
      i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
      i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
      i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n];
      i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
      i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
      NL[node][t+512]=a1; NH[node][t+512]=a2; } }
  // feasibility per family
  auto inter=[&](int lo_n,int hi_n,int t,i64&gl,i64&gh){ gl=0;gh=12000000;
    for(int node=lo_n;node<=hi_n;node++){ if(!TB[node>=17?32-node:node].S)continue;
      if(NL[node][t+512]>gl)gl=NL[node][t+512]; if(NH[node][t+512]<gh)gh=NH[node][t+512]; } };
  int fa=0,fu=0,ff=0; std::vector<int> bad_all,bad_u,bad_f;
  for(int t=-512;t<=511;t++){ i64 gl,gh;
    inter(1,31,t,gl,gh); if(gl<=gh)fa++; else bad_all.push_back(t);
    inter(1,16,t,gl,gh); if(gl<=gh)fu++; else bad_u.push_back(t);
    inter(17,31,t,gl,gh); if(gl<=gh)ff++; else bad_f.push_back(t); }
  printf("feasible t: all 31 nodes %d ; unflipped 1..16 only %d ; flipped 17..31 only %d\n",fa,fu,ff);
  printf("infeasible with all: "); for(int t:bad_all)printf("%d ",t); printf("\n");
  printf("infeasible unflipped: "); for(int t:bad_u)printf("%d ",t); printf("\n");
  printf("infeasible flipped:   "); for(int t:bad_f)printf("%d ",t); printf("\n");
  // do the two families disagree anywhere?
  int disagree=0; std::vector<int> dg;
  for(int t=-512;t<=511;t++){ i64 ul,uh,fl,fh; inter(1,16,t,ul,uh); inter(17,31,t,fl,fh);
    if(ul>uh||fl>fh) continue; if(uh<fl||fh<ul){disagree++; if(dg.size()<40)dg.push_back(t);} }
  printf("t where unflipped and flipped families have DISJOINT J intervals: %d :",disagree);
  for(int t:dg)printf(" %d",t); printf("\n");
  // per-node bias: how often is the node's interval entirely below / above the ideal round(KC t^2/2^17)
  puts("\nnode  below  above   (interval strictly below / above the ideal J) ; avg signed midpoint offset");
  for(int node=1;node<=31;node++){ int n=node>=17?32-node:node; if(!TB[n].S)continue;
    int below=0,above=0; long double acc=0; int cnt=0;
    for(int t=-512;t<=511;t++){ if(NL[node][t+512]>NH[node][t+512])continue;
      long double ideal=(long double)KC*t*t/131072.0L;
      if(NH[node][t+512]<ideal-0.5L)below++; if(NL[node][t+512]>ideal+0.5L)above++;
      acc += ((long double)NL[node][t+512]+NH[node][t+512])/2.0L - ideal; cnt++; }
    printf("%3d %6d %6d    %+9.3Lf\n",node,below,above,acc/cnt); }
  // interesting t
  int look[]={17,-17,40,-40,194,-194,342,-342,376,-376,258,-258};
  for(int L=0;L<12;L++){ int t=look[L]; printf("\nt=%d  ideal J=%.3f\n",t,(double)KC*t*t/131072.0);
    for(int node=1;node<=31;node++){ int n=node>=17?32-node:node; if(!TB[n].S)continue;
      i64 a=NL[node][t+512],b=NH[node][t+512]; if(b-a>400)continue;
      printf("   node %2d (tab %2d%s): [%lld, %lld] w=%lld\n",node,n,node>=17?",flip":"     ",(long long)a,(long long)b,(long long)(b-a)); } }
  return 0; }
