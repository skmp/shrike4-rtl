// c8_two2.cpp -- the two-ladder family again, in the region the arithmetic actually allows.
// Two constraints fight each other and together pin the scale:
//   t = 17   : J must be 45617 against an exact 45628.74, so the square path must LOSE 11.74 latch units;
//              the two floors can lose at most 17*(alpha+beta)/2^s3.
//   |t| = 342: D = J(-342)-J(342) must be in [22,31]; the ladder steps are 342*alpha/2^s3, 342*beta/2^s3.
// Together with alpha*beta = 157.88*2^s3 these force s3 <= 11 and alpha ~ 16..192, beta ~ 1687..2571.
// (c5 scanned alpha in [1500,2900], s3 in [13,23] -- entirely outside this region, which is why 492M configs
//  passed both signs of 342 and not one passed t=17.)
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024];
static bool load(const char*p){ FILE*f=fopen(p,"r"); if(!f)return false; char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; int t; long long a,b,c;
        if(sscanf(ln,"%d %lld %lld %lld",&t,&a,&b,&c)>=3){JL[t+512]=a;JH[t+512]=b;} } fclose(f); return true; }
static inline i64 Jof(i64 C1,int s1,i64 K1,i64 C2,int s2,i64 K2,int s3,i64 K3,int t){
    i64 dx=(i64)t<<13; i64 L1=(arr(C1,dx)+K1)>>s1, L2=(arr(C2,dx)+K2)>>s2; return (arr(L1,L2)+K3)>>s3; }
int main(){ build_tables(); if(!load("jwin18.txt")){puts("no jwin18.txt");return 1;}
    printf("targets: J(17)=[%lld,%lld]  J(342)=[%lld,%lld]  J(-342)=[%lld,%lld]\n",
        (long long)JL[17+512],(long long)JH[17+512],(long long)JL[342+512],(long long)JH[342+512],
        (long long)JL[-342+512],(long long)JH[-342+512]);
    long long tried=0,p17=0,p342=0,pboth=0; int best=-1;
    struct B{ i64 C1,C2; int s1,s2,s3,k1,k2,k3; } bb{};
    std::vector<B> hits;
    #pragma omp parallel
    { int lb=-1; B lbb{}; long long lt=0,la=0,lc=0,ld=0; std::vector<B> lh;
      #pragma omp for schedule(dynamic) collapse(2)
      for(int s1=20;s1<=27;s1++) for(int s3=8;s3<=12;s3++){
        i64 sh=1ll<<(s1-13);
        i64 c1lo=(i64)(8.0*sh), c1hi=(i64)(260.0*sh);       // alpha in [8,260]
        for(i64 C1=c1lo;C1<=c1hi;C1++){
          for(int s2=18;s2<=30;s2++){ int m=s1+s2+s3-41; if(m<0||m>46) continue;
            __int128 num=(__int128)KC<<m; i64 C2n=(i64)(num/C1);
            if(C2n<(1ll<<18)||C2n>=(1ll<<26)) continue;
            for(int k1=0;k1<2;k1++) for(int k2=0;k2<2;k2++) for(int k3=0;k3<2;k3++){
              i64 K1=k1?(1ll<<19):0,K2=k2?(1ll<<19):0,K3=k3?(1ll<<19):0;
              for(i64 d=-3;d<=3;d++){ i64 C2=C2n+d; lt++;
                i64 j17=Jof(C1,s1,K1,C2,s2,K2,s3,K3,17);
                if(j17<JL[17+512]||j17>JH[17+512]) continue;  la++;
                i64 jp=Jof(C1,s1,K1,C2,s2,K2,s3,K3,342);
                if(jp<JL[342+512]||jp>JH[342+512]) continue;  lc++;
                i64 jm=Jof(C1,s1,K1,C2,s2,K2,s3,K3,-342);
                if(jm<JL[-342+512]||jm>JH[-342+512]) continue; ld++;
                int sc=0; for(int t=-512;t<=511;t++){ int i=t+512; if(JL[i]>JH[i])continue;
                  i64 J=Jof(C1,s1,K1,C2,s2,K2,s3,K3,t); if(J>=JL[i]&&J<=JH[i]) sc++; }
                if(sc>lb){ lb=sc; lbb={C1,C2,s1,s2,s3,k1,k2,k3}; }
                if(sc>=1000) lh.push_back({C1,C2,s1,s2,s3,k1,k2,k3});
              } } } } } 
      #pragma omp critical
      { tried+=lt; p17+=la; p342+=lc; pboth+=ld; if(lb>best){best=lb;bb=lbb;} hits.insert(hits.end(),lh.begin(),lh.end()); }
    }
    printf("tried %lld | passed t=17 %lld | +t=342 %lld | +t=-342 %lld\n",tried,p17,p342,pboth);
    printf("best score %d / 1024\n",best);
    if(best>0){ printf("  C1=%lld s1=%d K1=%s | C2=%lld s2=%d K2=%s | s3=%d K3=%s\n",
        (long long)bb.C1,bb.s1,bb.k1?"2^19":"0",(long long)bb.C2,bb.s2,bb.k2?"2^19":"0",bb.s3,bb.k3?"2^19":"0");
        printf("  alpha=%.4f beta=%.4f  alpha*beta=%.2f  157.88*2^%d=%.2f\n",
            (double)bb.C1/(double)(1ll<<(bb.s1-13)),(double)bb.C2/(double)(1ll<<(bb.s2-13)),
            (double)bb.C1/(1ll<<(bb.s1-13))*(double)bb.C2/(1ll<<(bb.s2-13)),bb.s3,157.8835*(1ll<<bb.s3)); }
    printf("configs >= 1000: %zu\n",hits.size());
    return 0; }
