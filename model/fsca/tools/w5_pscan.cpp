#include "base2.h"
#include <cstdio>
static i64 JL[1024],JH[1024]; static bool have[1024];
int main(){ build_tables(); FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f);
    printf("p at which the UNIFORM dx offset reaches J(17)=45617, and what it costs:\n");
    for(int p=-40;p<=0;p++){ int s=0; i64 j17=0,j342=0;
        for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; i64 dx=((i64)t<<13)+p;
            i64 I1=(arr(dx,KC)+K)>>23; i64 j=(arr(I1,dx)+K)>>18;
            if(t==17)j17=j; if(t==342)j342=j; if(j>=JL[i]&&j<=JH[i])s++; }
        if(j17<=45620||p==0) printf("  p=%+3d  J(17)=%lld  J(342)=%lld  score %4d/1024\n",p,(long long)j17,(long long)j342,s); }
    // exact baseline pos/neg split for BOTH operand orders
    for(int ord=0;ord<2;ord++){ int pos=0,neg=0;
        for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; i64 dx=(i64)t<<13;
            i64 I1=ord?((arr(dx,KC)+K)>>23):((arr(KC,dx)+K)>>23); i64 j=(arr(I1,dx)+K)>>18;
            if(j>=JL[i]&&j<=JH[i]){ if(t>=0)pos++; else neg++; } }
        printf("order %s : pos %d/512 neg %d/512 total %d ; sign-dependent CEILING = %d\n",
            ord?"arr(dx,KC)":"arr(KC,dx)",pos,neg,pos+neg,pos+512); }
    return 0; }
