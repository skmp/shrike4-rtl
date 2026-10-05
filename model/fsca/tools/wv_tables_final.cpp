// wv_tables_final.cpp -- complete the claim's own table and the matched-rounding comparison.
#include "wv_tables_core.h"
#include <cstdio>
static int M1_of(const WCfg& C, int* pn){ int m=0;
  for(int node=0;node<=31;node++){int flip=node>=17,n=flip?32-node:node;
    for(int t=-512;t<=511;t++){int a=(node*1024+t)&0xffff;i64 lo,hi;bool ex;wv_win(a,lo,hi,ex);
      i64 V=wv_V(C,n,t,flip); if(V<lo||V>hi){m++;if(pn)pn[node]++;}}} return m;}
int main(){ if(!load_fsca(WF))return 1; wv_build();
  puts("the claim's own table, re-measured, plus the matched-rounding column it omits:");
  puts("  s2   u2       M1 (k2=2^19, the multiplier's own product)   M1 (k2=0)   guard@k2=2^19   guard@k2=0");
  for(int s2=17;s2<=21;s2++){
    WCfg A; A.s2=s2; A.k2=1ll<<19; int pa[32]={0}; int ma=M1_of(A,pa);
    WCfg B; B.s2=s2; B.k2=0;       int pb[32]={0}; int mb=M1_of(B,pb);
    bool ga = !(pa[0]||pa[1]||pa[2]||pa[16]||pa[30]||pa[31]);
    bool gb = !(pb[0]||pb[1]||pb[2]||pb[16]||pb[30]||pb[31]);
    printf("  %2d  %2d+k              %4d                             %4d        %s          %s\n",
       s2,50-s2,ma,mb,ga?"exact":"BROKEN",gb?"exact":"BROKEN"); }
  return 0; }
