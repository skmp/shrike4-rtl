// v52_loss.cpp -- sanity check on the loss law the B(iv) framing rests on:
//   arr(rec,mul) - rec*mul  depends only on (rec mod 2^18, mul mod 2^16)?
#include "v52_common.h"
#include <map>
#include <random>
int main(){ build_tables(); std::map<std::pair<i64,i64>,i64> L; long long n=0,bad=0;
  std::mt19937_64 g(12345);
  for(long long i=0;i<400000;i++){ i64 rec=(i64)(g()%(1ll<<23)), mul=(i64)(g()%(1ll<<23));
    i64 d=arr(rec,mul)-rec*mul; auto k=std::make_pair(rec&((1ll<<18)-1),mul&((1ll<<16)-1));
    auto it=L.find(k); if(it==L.end())L[k]=d; else if(it->second!=d)bad++; n++; }
  printf("positive operands: %lld samples, %lld contradictions of the loss law, %zu distinct keys\n",n,bad,L.size());
  // does it hold where FSCA actually uses it (negative recoded operand, dx = t<<13)?
  std::map<std::pair<i64,i64>,i64> L2; long long bad2=0,n2=0;
  for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23;
    for(int which=0;which<2;which++){ i64 rec=which?I1:KC, mul=dx; i64 d=arr(rec,mul)-rec*mul;
      auto k=std::make_pair(((rec%(1ll<<18))+(1ll<<18))%(1ll<<18), ((mul%(1ll<<16))+(1ll<<16))%(1ll<<16));
      auto it=L2.find(k); if(it==L2.end())L2[k]=d; else if(it->second!=d)bad2++; n2++; } }
  printf("FSCA's own uses (signed): %lld samples, %lld contradictions, %zu distinct keys\n",n2,bad2,L2.size());
  // how many distinct dx mod 2^16 values at each alignment?
  for(int w=10;w<=16;w++){ std::map<i64,int> s; for(int t=-512;t<=511;t++) s[(((i64)t<<w)%(1ll<<16)+(1ll<<16))%(1ll<<16)]++;
    printf("  w=%2d : dx mod 2^16 takes %zu distinct values over the 1024 offsets\n",w,s.size()); }
  return 0; }
