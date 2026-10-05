// v20_max.cpp -- is 925/1024 really the fixed-K (K1=K2=2^19) ceiling over ALL alignments?
// For every scale-admissible (w,s1,s2), sweep KCw over +-30 ppm of nominal (far wider than any
// scoring configuration can survive: a relative change d in KCw moves J(512) ~ 41.4e6*d, and that
// window is 76 wide, so |d| > 2e-6 already loses the largest offsets).  Exact early-abort scoring.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
typedef __int128 i128;
static i64 JL[1024],JH[1024];
int main(){
    { FILE*f=fopen("jwin18.txt","r");
      char ln[512]; long long t,l,h; while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue;
      if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){JL[t+512]=l;JH[t+512]=h;} } fclose(f); }
    const int FLOOR=918;                 // report anything that beats the baseline
    int gbest=0; struct R{int w,s1,s2;i64 kc;int sc;} GB{0,0,0,0,0};
    long long scanned=0, tri=0;
    #pragma omp parallel for schedule(dynamic) collapse(2) reduction(+:scanned,tri)
    for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++) for(int s2=4;s2<=40;s2++){
        int e=s1+s2-2*w-15; if(e<-30) continue;
        i128 nom=e>=0?((i128)KC<<e):((i128)KC>>(-e)); if(nom<1||nom>=((i128)1<<27)) continue;
        tri++;
        i64 n=(i64)nom; i64 span=std::max((i64)64,(i64)(n/33333));   // ~30 ppm
        int lbest=0; i64 lkc=0;
        for(i64 kc=std::max((i64)1,n-span); kc<=n+span; kc++){ scanned++;
            int miss=0, sc=0;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<w; i64 I1=(arr(kc,dx)+K)>>s1; i64 J=(arr(I1,dx)+K)>>s2;
                if(J>=JL[t+512]&&J<=JH[t+512]) sc++; else if(++miss>1024-FLOOR) break; }
            if(sc>lbest){ lbest=sc; lkc=kc; }
        }
#pragma omp critical
        { if(lbest>GB.sc) GB=R{w,s1,s2,lkc,lbest};
          if(lbest>925) printf("  >925: w=%d s1=%d s2=%d KCw=%lld -> %d\n",w,s1,s2,(long long)lkc,lbest); }
    }
    printf("triples %lld ; KCw scanned %lld\n",tri,scanned);
    printf("BEST fixed-K (K1=K2=2^19) jwin18 score over all alignments: %d  (w=%d s1=%d s2=%d KCw=%lld)\n",
        GB.sc,GB.w,GB.s1,GB.s2,(long long)GB.kc);
    return 0;
}
