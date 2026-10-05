// v52_align.cpp -- the COMPLETE "extra bit / different alignment" family at the published constant's own
// scale: dx = t<<w, I1=(arr.,.)>>s1, J=(arr(I1,dx))>>s2 with s1+s2 = 15+2w (so the nominal stored constant
// is exactly 0x4EF145 at every w), constant swept +-4096, both operand orders.  Scored on jwin18.
#include "v52_common.h"
#include <algorithm>
#include <omp.h>
static JW W;
static inline int sc(int w,int s1,int s2,i64 kc,int o){ int s=0;
  for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<w; i64 I1= o?(arr(dx,kc)+K)>>s1:(arr(kc,dx)+K)>>s1;
    i64 j=(arr(I1,dx)+K)>>s2; if(j>=W.lo[t+512]&&j<=W.hi[t+512])s++; } return s; }
int main(){ if(!load_jwin(W))return 1; build_tables();
  long long cfgs=0; int gb=-1,gw=0,gs1=0,gs2=0,go=0; i64 gk=0;
  struct H{int w,s1,s2,o,s; i64 k;}; std::vector<H> hits;
  for(int w=9;w<=18;w++) for(int s1=8;s1<=36;s1++){ int s2=15+2*w-s1; if(s2<8||s2>36)continue;
    for(int o=0;o<2;o++){ int best=-1; i64 bk=0;
      #pragma omp parallel
      { int lb=-1; i64 lk=0;
        #pragma omp for schedule(static)
        for(long long i=0;i<8193;i++){ i64 kc=KC-4096+i; if(kc<1)continue; int s=sc(w,s1,s2,kc,o); if(s>lb){lb=s;lk=kc;} }
        #pragma omp critical
        { if(lb>best){best=lb;bk=lk;} } }
      cfgs+=8193;
      if(best>gb){gb=best;gw=w;gs1=s1;gs2=s2;go=o;gk=bk;}
      if(best>=918) hits.push_back(H{w,s1,s2,o,best,bk}); } }
  printf("configuration space: %lld (w,s1,order,KC) points, s2 determined by s1+s2=15+2w\n",cfgs);
  printf("GLOBAL BEST: %d/1024 at w=%d s1=%d s2=%d order=%d KC=%lld (published %lld)\n",gb,gw,gs1,gs2,go,(long long)gk,(long long)KC);
  puts("every config scoring >= 918 (baseline):");
  for(auto&h:hits){ i64 j17,j342,jm; { i64 dx=(i64)17<<h.w; i64 I1=h.o?(arr(dx,h.k)+K)>>h.s1:(arr(h.k,dx)+K)>>h.s1; j17=(arr(I1,dx)+K)>>h.s2;
      dx=(i64)342<<h.w; I1=h.o?(arr(dx,h.k)+K)>>h.s1:(arr(h.k,dx)+K)>>h.s1; j342=(arr(I1,dx)+K)>>h.s2;
      dx=(i64)-342<<h.w; I1=h.o?(arr(dx,h.k)+K)>>h.s1:(arr(h.k,dx)+K)>>h.s1; jm=(arr(I1,dx)+K)>>h.s2; }
    printf("  w=%2d s1=%2d s2=%2d o=%d KC=%8lld -> %d/1024  J(17)=%lld J(342)=%lld J(-342)=%lld\n",
      h.w,h.s1,h.s2,h.o,(long long)h.k,h.s,(long long)j17,(long long)j342,(long long)jm); }
  return 0; }
