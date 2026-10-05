// v10_cub2.cpp -- STRONGER model-free certificate: scan EVERY equally spaced 5-point subset
// (start, spacing) inside each candidate interval, not just the quartile points.
#include "base2.h"
#include <cstring>
#include <algorithm>
#include <cstdio>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]); if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}
static i64 LO[65536],HI[65536];
static double best_cert(int a0,int L){   // max over all (start,h) of the |D4| lower bound / 16
  static const int C[5]={1,-4,6,-4,1}; i64 bg=0;
  for(int h=1;h*4<=L;h++) for(int s=0;s+4*h<=L;s++){ i64 mn=0,mx=0;
     for(int i=0;i<5;i++){ int a=(a0+s+i*h)&0xffff; i64 c=C[i];
        if(c>0){mn+=c*LO[a];mx+=c*HI[a];} else {mn+=c*HI[a];mx+=c*LO[a];} }
     i64 g=(mn>0)?mn:((mx<0)?-mx:0); if(g>bg)bg=g; }
  return (double)bg/16.0; }
int main(){ if(!load_fsca(F))return 1;
  for(int a=0;a<65536;a++){ i64 lo,hi; win_of(a,lo,hi); LO[a]=lo; HI[a]=hi; }
  puts("=== v10_cub2: max over ALL equally spaced 5-point subsets ===");
  puts("value = rigorous lower bound on the best cubic's max error on that interval, in 2^-26 units\n");
  for(int L : {1024,2048}){
    printf("--- span L=%d ---\n",L);
    double bm=1e18; int bp=-1;
    for(int p=0;p<L;p+= (L==1024?64:128)){
      double sum=0,worst=0; int cnt=0;
      for(int k=0;;k++){ int a0=p+k*L; if(a0+L>16384)break; double s=best_cert(a0,L); sum+=s; worst=std::max(worst,s); cnt++; }
      printf("  p=%4d  mean %8.3f  worst %8.3f  (%d intervals)\n",p,sum/cnt,worst,cnt);
      if(sum/cnt<bm){bm=sum/cnt;bp=p;} }
    printf("  -> minimum mean %.3f at p=%d\n\n",bm,bp); }
  return 0; }
