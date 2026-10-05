// s1_backparam.cpp -- SYMBOLIC BACK-PROPAGATION TO THE PARAMETERS.
// For each offset independently: what would KC have had to be for J to land in its window?
// NOT a binary search: arr(v,dx) is non-monotone in the recoded operand v, so J is not monotone in
// KC. Exhaustive sweep over a KC band instead.
// Also: the K1 branch analysis (I1 takes only TWO values as K1 sweeps its whole range, so each offset
// simply votes for the low branch, the high branch, both, or neither).
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024];
static const i64 KCM=0x4ef145;
int main(){
    FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} }
    fclose(f);
    const i64 R=20000;
    std::vector<int> cover(2*R+1,0);           // how many offsets each KC satisfies
    std::vector<int> nsol(1024,0);             // how many KC in band work for this offset
    std::vector<i64> firstK(1024,-1), lastK(1024,-1);
    int modelok=0, noneok=0;
    for(int i=0;i<1024;i++){ if(!have[i]) continue; int t=i-512; i64 dx=(i64)t<<13;
        for(i64 d=-R;d<=R;d++){ i64 kc=KCM+d;
            i64 I1=(arr(dx,kc)+K)>>23; i64 j=(arr(I1,dx)+K)>>18;
            if(j>=JL[i]&&j<=JH[i]){ cover[d+R]++; nsol[i]++; if(firstK[i]<0)firstK[i]=kc; lastK[i]=kc; } }
        if(nsol[i]==0) noneok++;
    }
    { i64 I1,j; for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; i64 dx=(i64)t<<13;
        I1=(arr(dx,KCM)+K)>>23; j=(arr(I1,dx)+K)>>18; if(j>=JL[i]&&j<=JH[i]) modelok++; } }
    int best=0; i64 bestkc=0; for(i64 d=-R;d<=R;d++) if(cover[d+R]>best){best=cover[d+R];bestkc=KCM+d;}
    printf("KC back-propagated per offset, exhaustive over KC in [%lld, %lld] (%lld values):\n",
        (long long)(KCM-R),(long long)(KCM+R),(long long)(2*R+1));
    printf("  model KC = 0x%llX satisfies %d / 1024 offsets\n",(unsigned long long)KCM,modelok);
    printf("  BEST KC in the band = %lld (0x%llX, model %+lld) satisfies %d / 1024\n",
        (long long)bestkc,(unsigned long long)bestkc,(long long)(bestkc-KCM),best);
    printf("  offsets NO KC in the band can satisfy: %d\n",noneok);
    // how wide is each offset's KC solution set, and where does it sit?
    std::vector<i64> mid; int nz=0;
    for(int i=0;i<1024;i++) if(have[i]&&nsol[i]){ nz++; mid.push_back((firstK[i]+lastK[i])/2 - KCM); }
    std::sort(mid.begin(),mid.end());
    printf("\n  offsets with a nonempty KC set: %d ; centre of that set relative to the model KC:\n",nz);
    printf("    min %lld  p10 %lld  median %lld  p90 %lld  max %lld\n",
        (long long)mid.front(),(long long)mid[mid.size()/10],(long long)mid[mid.size()/2],
        (long long)mid[mid.size()*9/10],(long long)mid.back());
    // the offsets the model's KC FAILS: where does their KC set sit?
    printf("\n  the %d offsets the model's KC fails -- where do their required KC sets sit?\n",1024-modelok);
    int lo=0,hi=0,both=0,none=0;
    for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; i64 dx=(i64)t<<13;
        i64 I1=(arr(dx,KCM)+K)>>23; i64 j=(arr(I1,dx)+K)>>18; if(j>=JL[i]&&j<=JH[i]) continue;
        if(!nsol[i]){none++;continue;}
        bool below=firstK[i]<KCM, above=lastK[i]>KCM;
        if(below&&above) both++; else if(below) lo++; else hi++; }
    printf("    need KC LOWER: %d    need KC HIGHER: %d    straddle: %d    no KC works: %d\n",lo,hi,both,none);
    printf("    (a single wrong constant would put them ALL on one side)\n");
    // K1 branch vote
    printf("\nK1 branch analysis: as K1 sweeps [0,2^23) the first latch takes only TWO values.\n");
    int vlo=0,vhi=0,vboth=0,vnone=0;
    for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; i64 dx=(i64)t<<13; i64 P=arr(dx,KCM);
        i64 a=P>>23, b=a+1; bool oka,okb;
        { i64 j=(arr(a,dx)+K)>>18; oka=(j>=JL[i]&&j<=JH[i]); }
        { i64 j=(arr(b,dx)+K)>>18; okb=(j>=JL[i]&&j<=JH[i]); }
        if(oka&&okb)vboth++; else if(oka)vlo++; else if(okb)vhi++; else vnone++; }
    printf("  low branch only %d   high branch only %d   either %d   NEITHER %d\n",vlo,vhi,vboth,vnone);
    printf("  -> %d offsets cannot be fixed by ANY first-latch rounding constant.\n",vnone);
    return 0; }
