// v22_fixk.cpp -- independent exhaustive sweep of the stored constant at every alignment with the
// rounding constants held at the physical 2^19: does anything beat the branch's reported 925?
// Two-stage: a 128-offset prefilter, then the full 1024-offset score on survivors.
#include "base2.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024];
static void load_jwin(){ FILE*f=fopen("jwin18.txt","r"); if(!f)exit(1); char ln[256]; int n=0;
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
    if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){JL[t+512]=l;JH[t+512]=h;n++;} } fclose(f); if(n!=1024)exit(1); }
struct Cfg{ int w,s1,s2; i64 kc; };
static inline i64 Jof(int t,const Cfg&c){ i64 dx=(i64)t<<c.w; i64 I1=(arr(c.kc,dx)+K)>>c.s1; return (arr(I1,dx)+K)>>c.s2; }
// full score with early abort once it can no longer reach `floor` (no sampling: a strided sample
// would be biased, since only t mod 8 reaches the first multiply's truncated columns).
static inline int full_ea(const Cfg&c,int floor_){ int s=0,miss=0; for(int i=0;i<1024;i++){ i64 j=Jof(i-512,c);
    if(j>=JL[i]&&j<=JH[i])s++; else { if(++miss>1024-floor_) return -1; } } return s; }
static inline int full(const Cfg&c){ int s=0; for(int i=0;i<1024;i++){ i64 j=Jof(i-512,c); if(j>=JL[i]&&j<=JH[i])s++; } return s; }
int main(){ build_tables(); load_jwin();
    int gbest=0; Cfg gc{}; long long ntrip=0,nkc=0;
    std::vector<Cfg> trips;
    for(int w=6;w<=18;w++){ if(llabs((i64)512<<w)>=(1ll<<27))continue;
      for(int s2=(w>4?w-4:1);s2<=w+24;s2++) for(int s1=4;s1<=2*w+30;s1++){
        long double ctr=157.88623046875L*powl(2.0L,(long double)(s1+s2-2*w));
        if(ctr<16384.0L||ctr>=134217728.0L)continue;
        i64 dxB=(i64)342<<w, per1=1ll<<s1;
        i64 I1p=(arr((i64)llroundl(ctr),dxB)+K)>>s1; if(llabs(I1p)>=(1ll<<27))continue;
        trips.push_back({w,s1,s2,(i64)llroundl(ctr)}); } }
    ntrip=trips.size();
    printf("triples in space: %lld ; sweeping KCw over +- max(2048, 5e-5*centre)\n",ntrip);
    #pragma omp parallel
    { int lb=0; Cfg lc{}; long long lk=0;
      #pragma omp for schedule(dynamic)
      for(size_t i=0;i<trips.size();i++){ Cfg c=trips[i]; i64 ctr=c.kc;
        i64 span=(i64)(ctr*5e-5)+2048;
        for(i64 kc=ctr-span;kc<=ctr+span;kc++){ if(kc<1)continue; c.kc=kc; lk++;
          int s=full_ea(c, lb>900?lb:900); if(s>lb){lb=s;lc=c;} } }
      #pragma omp critical
      { nkc+=lk; if(lb>gbest){gbest=lb;gc=lc;} } }
    printf("KCw values evaluated: %lld\n",nkc);
    printf("BEST with K1=K2=2^19 : jwin18 %d/1024 at w=%d s1=%d s2=%d KCw=%lld\n",gbest,gc.w,gc.s1,gc.s2,(long long)gc.kc);
    printf("  rescaled to the KC scale, KCw*2^(15+2w-s1-s2) = %.6Lf   (KC = 5173573)\n",
        (long double)gc.kc*powl(2.0L,(long double)(15+2*gc.w-gc.s1-gc.s2)));
    printf("  landmarks J(17)=%lld J(342)=%lld J(-342)=%lld\n",(long long)Jof(17,gc),(long long)Jof(342,gc),(long long)Jof(-342,gc));
    return 0; }
