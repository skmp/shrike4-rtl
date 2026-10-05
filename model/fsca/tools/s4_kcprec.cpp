// s4_kcprec.cpp -- motivated by the backward trace: the required KC is 1 TOO LOW at some offsets and
// 1 TOO HIGH at others, so no 24-bit value satisfies both. Does a WIDER stored constant?
//   I1 = (arr(dx, KCb) + K1) >> (23 + b)     KCb ~ KC * 2^b   (b extra bits of constant precision)
// b=0 reproduces the model exactly. K1 tested both fixed at 2^19 and scaled to 2^(19+b).
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024]; static int NT=0,TS[1024];
static const i64 KCM=0x4ef145;
int main(){
    FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} }
    fclose(f);
    for(int i=0;i<1024;i++) if(have[i]) TS[NT++]=i-512;
    printf("extra constant precision b:  KCb ~ KC*2^b,  I1 = (arr(dx,KCb)+K1) >> (23+b)\n");
    printf("  b   K1 rule      band        best KCb        KCb/2^b        score\n");
    int gbest=0;
    for(int b=0;b<=8;b++){
      for(int kr=0;kr<2;kr++){
        i64 K1 = kr? (K<<b) : K;
        i64 C0v=KCM<<b; i64 R=300LL<<b; if(b==0)R=20000;
        int best=-1; i64 bestc=0;
        #pragma omp parallel for schedule(static)
        for(i64 d=-R;d<=R;d++){ i64 kc=C0v+d; int sc=0;
            for(int i=0;i<NT;i++){ int t=TS[i]; i64 dx=(i64)t<<13;
                i64 I1=(arr(dx,kc)+K1)>>(23+b);
                i64 j=(arr(I1,dx)+K)>>18; int k=t+512;
                if(j>=JL[k]&&j<=JH[k]) sc++; }
            #pragma omp critical
            if(sc>best){ best=sc; bestc=kc; }
        }
        printf("  %d   %-11s +-%-9lld %-15lld %-14.4f %d%s\n",b,kr?"2^(19+b)":"2^19 fixed",
            (long long)R,(long long)bestc,(double)bestc/(double)(1LL<<b),best,
            best>gbest?"   <== best so far":"");
        if(best>gbest) gbest=best;
      }
    }
    printf("\nbaseline (b=0, K1=2^19, KC=0x4EF145) = 918/1024\n");
    printf("best overall = %d/1024\n",gbest);
    return 0; }
