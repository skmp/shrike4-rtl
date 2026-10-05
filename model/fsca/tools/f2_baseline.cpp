// f2_baseline.cpp -- re-measure the published baseline end to end, with the guard nodes broken out,
// so every claim in this session is anchored to a number run here.  No model file was modified.
#include "base2.h"
#include <cstring>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
int main(){ if(!load_fsca(F))return 1; build_tables();
  for(int n=0;n<=16;n++) TB[n].C0=C0TAB[n];
  int tot=0, per[32]; memset(per,0,sizeof per);
  for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
      i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
      i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
      i64 I1=(arr(dx,KC)+K)>>23; i64 J=(arr(I1,dx)+K)>>20;
      i64 T2=TB[n].S? sm(arr(J,TB[n].S)+K,TB[n].u2):0;
      i64 V=TB[n].C0+s*T1-T2-s*T3;
      int a=(node*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi;
      if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}
      if(V<lo||V>hi){per[node]++;tot++;} } }
  printf("published model, nodes 0..31, arr(dx,KC): %d window violations\n",tot);
  printf("guard nodes: 0 -> %d   1 -> %d   2 -> %d   16 -> %d   (mirrors 32,31,30,16)\n",per[0],per[1],per[2],per[16]);
  printf("per-node: "); for(int n=0;n<32;n++) if(per[n]) printf("%d:%d ",n,per[n]); printf("\n");
  return 0; }
