// wv_freeform_sweep.cpp -- independent CLEAN-constant chain sweep: can any unfitted chain beat 936?
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static int OKT[1024];
int main(){
  { FILE*f=fopen("jwin.txt","r"); for(int i=0;i<1024;i++){int t,ok;long long a,b;
      if(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&ok)!=4)return 1; JL[t+512]=a;JH[t+512]=b;OKT[t+512]=ok;} fclose(f);}
  struct R{int sc,s1,s2,k1,k2,r1,r2,g;};
  std::vector<R> all;
  #pragma omp parallel
  { std::vector<R> loc;
    #pragma omp for schedule(dynamic)
    for(int s1=18;s1<=28;s1++) for(int s2=16;s2<=24;s2++)
      for(int k1=0;k1<3;k1++) for(int k2=0;k2<3;k2++)
        for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(int g=0;g<4;g++){
          i64 A = KC<<g;                                  // CLEAN constant only: KC scaled by a power of two
          i64 K1 = k1==0?0 : (k1==1? (1ll<<19) : (1ll<<(s1-1)));
          i64 K2 = k2==0?0 : (k2==1? (1ll<<19) : (1ll<<(s2-1)));
          int sc=0;
          for(int i=0;i<1024;i++){ if(!OKT[i])continue; int t=i-512; i64 dx=(i64)t<<13;
            i64 I1 = ((r1? arr(dx,A):arr(A,dx)) + K1) >> s1;
            i64 J  = ((r2? arr(dx,I1):arr(I1,dx)) + K2) >> s2;
            if(J>=JL[i]&&J<=JH[i])sc++; }
          loc.push_back({sc,s1,s2,(int)K1,(int)K2,r1,r2,g}); }
    #pragma omp critical
    for(auto&x:loc) all.push_back(x); }
  std::sort(all.begin(),all.end(),[](const R&a,const R&b){return a.sc>b.sc;});
  printf("clean-constant chain sweep: %zu configs (A = KC<<g only, no fitted offset)\n",all.size());
  for(int i=0;i<12;i++){ R&r=all[i];
    printf("  %4d/1022  s1=%d s2=%d K1=%d K2=%d role1=%s role2=%s A=KC<<%d\n",
      r.sc,r.s1,r.s2,r.k1,r.k2,r.r1?"arr(dx,A)":"arr(A,dx)",r.r2?"arr(dx,I1)":"arr(I1,dx)",r.g); }
  return 0; }
