// w5_b_bit.cpp -- BRANCH 5, HYPOTHESIS B(iii): "t carries one more bit of precision" read as
// "t's LSB sits one column higher/lower in the multiplier operand", i.e. dx = t << w for w != 13,
// with the stored constant KC FREE to compensate (the excluded chain family held KC at 0x4EF145 +-3).
// This is the only reading that changes WHICH bits of t reach the array's truncated columns
// (dx mod 2^16 = (t & ((1<<(16-w))-1)) << w).
#include "base2.h"
#include <cstdio>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024];
int main(){ build_tables();
    FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f);
    printf("  w   bits of t reaching the 1st multiply's loss   nominal KC'   best score   at KC'    J(17) J(342) J(-342)\n");
    for(int w=10;w<=16;w++){
        long long nom = (w<=13)? ((long long)0x4ef145<<(26-2*w)) : ((long long)0x4ef145>>(2*w-26));
        int best=-1; long long bk=0; long long bj17=0,bj342=0,bjm=0;
        long long span = 4096;
        for(long long kc=std::max(1LL,nom-span); kc<=nom+span; kc++){
            int s=0;
            for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; i64 dx=(i64)t<<w;
                i64 I1=(arr(dx,kc)+K)>>23; i64 j=(arr(I1,dx)+K)>>18;
                if(j>=JL[i]&&j<=JH[i])s++; }
            if(s>best){ best=s; bk=kc;
                for(int t : {17,342,-342}){ i64 dx=(i64)t<<w; i64 I1=(arr(dx,kc)+K)>>23; i64 j=(arr(I1,dx)+K)>>18;
                    if(t==17)bj17=j; else if(t==342)bj342=j; else bjm=j; } } }
        int nb = 16-w > 0 ? 16-w : 0;
        printf("  %2d   %d bit(s)                                %11lld   %4d/1024   %8lld  %lld %lld %lld%s\n",
            w,nb,nom,best,bk,(long long)bj17,(long long)bj342,(long long)bjm, w==13?"  <- published":"");
    }
    printf("  targets                                                                    45617 18466836 18466858..67\n");
    return 0; }
