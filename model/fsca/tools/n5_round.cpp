// n5_round.cpp -- EXHAUSTIVE over both latches' rounding constants.
//   I1 = (arr(dx,KC) + K1) >> s1      K1 in [0, 2^s1)
//   J  = (arr(I1,dx) + K2) >> s2      K2 in [0, 2^s2)
// As K1 sweeps [0,2^s1), I1(t) takes exactly TWO values with ONE threshold per t. Same for K2.
// So at most 1025 x 1025 distinct behaviours -- the scan below is complete over all rounding
// constants, not a sample.  K1 = 2^(s1-1) is true round-to-nearest; the model uses 2^19.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024]; static int NT=0; static int TS[1024];
static inline i64 nmod(i64 a,i64 m){ i64 r=a%m; return r<0?r+m:r; }
struct Best{ int score; i64 K1,K2; };
static Best run(int s1,int s2,bool verbose){
    const i64 M1=1LL<<s1, M2=1LL<<s2;
    std::vector<i64> P1(NT); std::vector<i64> th1(NT);
    for(int i=0;i<NT;i++){ int t=TS[i]; i64 dx=(i64)t<<13; P1[i]=arr(KC,dx);
        i64 r=nmod(P1[i],M1); th1[i] = r==0? M1 : (M1-r); }
    std::vector<i64> cand=th1; cand.push_back(0);
    std::sort(cand.begin(),cand.end()); cand.erase(std::unique(cand.begin(),cand.end()),cand.end());
    Best best{-1,0,0};
    for(i64 K1 : cand){ if(K1>=M1) continue;
        // I1 vector for this K1 regime
        std::vector<i64> J0(NT), th2(NT); std::vector<char> ok0(NT),ok1(NT);
        for(int i=0;i<NT;i++){ int t=TS[i]; i64 dx=(i64)t<<13;
            i64 I1=(P1[i]+K1)>>s1; i64 P2=arr(I1,dx);
            J0[i]=P2>>s2; i64 r=nmod(P2,M2); th2[i]= r==0? M2 : (M2-r);
            int k=t+512; ok0[i]= (J0[i]>=JL[k]&&J0[i]<=JH[k]); ok1[i]= (J0[i]+1>=JL[k]&&J0[i]+1<=JH[k]); }
        // sweep K2: start at K2=0 (all use J0), switch t to J0+1 when K2 reaches th2[t]
        std::vector<int> ord(NT); for(int i=0;i<NT;i++) ord[i]=i;
        std::sort(ord.begin(),ord.end(),[&](int a,int b){return th2[a]<th2[b];});
        int sc=0; for(int i=0;i<NT;i++) sc+=ok0[i];
        if(sc>best.score) best={sc,K1,0};
        int p=0; while(p<NT){ i64 k=th2[ord[p]]; if(k>=M2) break;
            while(p<NT && th2[ord[p]]==k){ int i=ord[p]; sc += (ok1[i]?1:0) - (ok0[i]?1:0); p++; }
            if(sc>best.score) best={sc,K1,k}; }
    }
    if(verbose) printf("  s1=%d s2=%d : best %4d/1024 at K1=%lld (=2^%.2f) K2=%lld (=2^%.2f)\n",
        s1,s2,best.score,(long long)best.K1, best.K1? __builtin_log2((double)best.K1):-1.0,
        (long long)best.K2, best.K2? __builtin_log2((double)best.K2):-1.0);
    return best;
}
int main(){
    FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} }
    fclose(f);
    for(int i=0;i<1024;i++) if(have[i]) TS[NT++]=i-512;
    printf("EXHAUSTIVE over both rounding constants (jwin18 = FINE scale, s1+s2 = 41)\n");
    printf("baseline model uses K1=K2=2^19=524288 at s1=23,s2=18 -> ");
    { i64 sc=0; for(int i=0;i<NT;i++){int t=TS[i];i64 dx=(i64)t<<13;i64 I1=(arr(KC,dx)+K)>>23;
        i64 j=(arr(I1,dx)+K)>>18; int k=t+512; if(j>=JL[k]&&j<=JH[k])sc++; } printf("%lld/1024\n\n",(long long)sc); }
    Best g{-1,0,0}; int gs1=0,gs2=0;
    for(int s1=14;s1<=30;s1++){ int s2=41-s1; if(s2<6||s2>30) continue;
        Best b=run(s1,s2,true); if(b.score>g.score){g=b;gs1=s1;gs2=s2;} }
    printf("\nBEST OVERALL %d/1024 at s1=%d s2=%d K1=%lld K2=%lld\n",g.score,gs1,gs2,(long long)g.K1,(long long)g.K2);
    printf("  (round-to-nearest would be K1=2^%d=%lld, K2=2^%d=%lld)\n",gs1-1,1LL<<(gs1-1),gs2-1,1LL<<(gs2-1));
    // landmark check at the best
    { i64 dx,I1,j; int bad=0;
      for(int t : {17,342,-342}){ dx=(i64)t<<13; I1=(arr(KC,dx)+g.K1)>>gs1; j=(arr(I1,dx)+g.K2)>>gs2;
        int k=t+512; printf("  J(%+d)=%lld  need [%lld,%lld] %s\n",t,(long long)j,(long long)JL[k],(long long)JH[k],
            (j>=JL[k]&&j<=JH[k])?"OK":"MISS"); if(!(j>=JL[k]&&j<=JH[k]))bad++; } (void)bad; }
    return 0; }
