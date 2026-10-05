// the case NOTES.md 2026-09-18 line 1301 claims to have run but did not:
//   mag = t>=0 ? t : ~t   (= |t|-1, a POSITIVE operand)  -- m5_mag.cpp's e=0 actually computes ~t+1 = |t|.
#include "base2.h"
#include <cstdio>
static i64 JL[1024],JH[1024]; static bool have[1024];
int main(){ build_tables(); FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f);
    const char* nm[3]={"mag = ~t   (|t|-1, positive operand)","mag = -t   (|t|, positive operand)","mag = t+1 (negative operand, TRUE 1s read)"};
    for(int v=0;v<3;v++){ int s=0; i64 j17=0,j342=0,jm=0;
        for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; i64 m;
            if(t>=0) m=t; else m = (v==0)? (~t) : (v==1? -t : t+1);
            i64 dx=m<<13; i64 I1=(arr(KC,dx)+K)>>23; i64 j=(arr(I1,dx)+K)>>18;
            if(t==17)j17=j; if(t==342)j342=j; if(t==-342)jm=j;
            if(j>=JL[i]&&j<=JH[i])s++; }
        printf("  %-46s : %4d/1024  J(17)=%lld J(342)=%lld J(-342)=%lld\n",nm[v],s,
            (long long)j17,(long long)j342,(long long)jm); }
    printf("  %-46s : %4d/1024\n","baseline (mag = t, 2s throughout)",918);
    return 0; }
