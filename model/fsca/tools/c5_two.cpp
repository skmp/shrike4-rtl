// c5_two.cpp -- TWO CONSTANTS, TWO LADDERS.
//   L1 = (arr(C1, dx) + K1) >> s1        ~ alpha * t     (one granularity)
//   L2 = (arr(C2, dx) + K2) >> s2        ~ beta  * t     (a DIFFERENT granularity)
//   J  = (arr(L1, L2) + K3) >> s3        ~ alpha*beta*t^2/2^s3 = KC t^2 / 2^15
// Both latches are proportional to t, so each contributes its own one-LSB sign asymmetry, and the total can be
// 0, alpha|t|/2^s3, beta|t|/2^s3 or their sum -- FOUR rungs instead of one.  That is the structural degree of
// freedom the two-multiply chain lacks (there, asymmetry and granularity are the same quantity).
// Physically natural: (2 pi) x (pi) = 2 pi^2, and 2*pi is one of the patent's three stored constants.
// Scale law: C1*C2 = KC * 2^(s1+s2+s3-41).  The D(342) window pins alpha into [1636, 2699], which is what makes
// the search tractable.  Filter on the pinned t=342 window (width 1) before the full score.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>
static i64 JL[1024],JH[1024];
static bool load(const char*p){ FILE*f=fopen(p,"r"); if(!f)return false; char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; int t; long long a,b,c;
        if(sscanf(ln,"%d %lld %lld %lld",&t,&a,&b,&c)>=3){JL[t+512]=a;JH[t+512]=b;} } fclose(f); return true; }
static inline i64 Jof(i64 C1,int s1,i64 K1,i64 C2,int s2,i64 K2,int s3,i64 K3,int t){
    i64 dx=(i64)t<<13; i64 L1=(arr(C1,dx)+K1)>>s1, L2=(arr(C2,dx)+K2)>>s2; return (arr(L1,L2)+K3)>>s3; }
int main(){ build_tables(); if(!load("jwin18.txt")){puts("no jwin18.txt");return 1;}
    printf("target at the pinned t: J(342) in [%lld,%lld], J(-342) in [%lld,%lld], J(17)=[%lld,%lld]\n",
        (long long)JL[342+512],(long long)JH[342+512],(long long)JL[-342+512],(long long)JH[-342+512],
        (long long)JL[17+512],(long long)JH[17+512]);
    long long tried=0,pass1=0,pass2=0; int best=-1;
    struct Best{ i64 C1,C2; int s1,s2,s3,k1,k2,k3; } bb{};
    std::vector<Best> hits;
    #pragma omp parallel
    { int lbest=-1; Best lbb{}; long long lt=0,l1=0,l2=0; std::vector<Best> lh;
      #pragma omp for schedule(dynamic)
      for(int s1=22;s1<=26;s1++){
        for(int s3=13;s3<=23;s3++){
          // alpha in [1500,2900] from the D(342) ladder window -> C1 = alpha * 2^(s1-13)
          i64 c1lo=(i64)1500<<(s1-13), c1hi=(i64)2900<<(s1-13);
          for(i64 C1=c1lo;C1<=c1hi;C1++){
            // choose s2 so that C2 lands in [2^22, 2^24)
            for(int s2=18;s2<=30;s2++){ int m=s1+s2+s3-41; if(m<0||m>46) continue;
              __int128 num=(__int128)KC<<m; i64 C2n=(i64)(num/C1); if(C2n<(1ll<<21)||C2n>=(1ll<<25)) continue;
              for(int k1=0;k1<2;k1++) for(int k2=0;k2<2;k2++) for(int k3=0;k3<2;k3++){
                i64 K1=k1?(1ll<<19):0,K2=k2?(1ll<<19):0,K3=k3?(1ll<<19):0;
                for(i64 d=-2;d<=2;d++){ i64 C2=C2n+d; lt++;
                  if(Jof(C1,s1,K1,C2,s2,K2,s3,K3,342)!=JL[342+512]) continue;   l1++;
                  i64 j2=Jof(C1,s1,K1,C2,s2,K2,s3,K3,-342);
                  if(j2<JL[-342+512]||j2>JH[-342+512]) continue;                l2++;
                  i64 j3=Jof(C1,s1,K1,C2,s2,K2,s3,K3,17);
                  if(j3<JL[17+512]||j3>JH[17+512]) continue;
                  int sc=0; for(int t=-512;t<=511;t++){ int i=t+512; if(JL[i]>JH[i])continue;
                    i64 J=Jof(C1,s1,K1,C2,s2,K2,s3,K3,t); if(J>=JL[i]&&J<=JH[i]) sc++; }
                  if(sc>lbest){ lbest=sc; lbb={C1,C2,s1,s2,s3,k1,k2,k3}; }
                  if(sc>=1000) lh.push_back({C1,C2,s1,s2,s3,k1,k2,k3});
                } } } } } }
      #pragma omp critical
      { tried+=lt; pass1+=l1; pass2+=l2; if(lbest>best){best=lbest;bb=lbb;} hits.insert(hits.end(),lh.begin(),lh.end()); }
    }
    printf("configs tried %lld ; passed t=342 %lld ; passed t=-342 too %lld\n",tried,pass1,pass2);
    printf("best score %d / 1024\n",best);
    if(best>0) printf("  C1=%lld s1=%d K1=%s | C2=%lld s2=%d K2=%s | s3=%d K3=%s   (alpha=%.2f beta=%.2f)\n",
        (long long)bb.C1,bb.s1,bb.k1?"2^19":"0",(long long)bb.C2,bb.s2,bb.k2?"2^19":"0",bb.s3,bb.k3?"2^19":"0",
        (double)bb.C1/(double)(1ll<<(bb.s1-13)),(double)bb.C2/(double)(1ll<<(bb.s2-13)));
    printf("configs scoring >= 1000: %zu\n",hits.size());
    return 0; }
