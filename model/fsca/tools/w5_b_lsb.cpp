// w5_b_lsb.cpp -- BRANCH 5, HYPOTHESIS B(i): t carries one extra LSB, i.e. the node base lands on a
// half-step:  dx = (2t + e) << 12   (e = 0 is exactly the published dx = t<<13).
// Scored (a) square path only against jwin18.txt, (b) whole datapath, full model, all 65536 angles.
#include "w5_model.h"
#include <cstdio>
static i64 JL[1024],JH[1024]; static bool have[1024];
static inline i64 chainJ(int t,int e,int order){
    i64 dx=((i64)2*t+e)<<12;
    i64 I1=order? ((arr(dx,KC)+K)>>23) : ((arr(KC,dx)+K)>>23);
    return (arr(I1,dx)+K)>>18; }
int main(){ if(!load_fsca(W5F))return 1; build_tables();
    FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f);
    printf("--- B(i)a: square path only, dx=(2t+e)<<12, jwin18 score (T1/T3 untouched so windows valid) ---\n");
    printf("   e    order=arr(KC,dx)   order=arr(dx,KC)    J(17)      J(342)     J(-342)\n");
    for(int e=-8;e<=8;e++){
        int s0=0,s1=0; for(int i=0;i<1024;i++) if(have[i]){ int t=i-512;
            i64 j0=chainJ(t,e,0), j1=chainJ(t,e,1);
            if(j0>=JL[i]&&j0<=JH[i])s0++; if(j1>=JL[i]&&j1<=JH[i])s1++; }
        printf("  %+2d      %4d/1024          %4d/1024      %8lld  %10lld  %10lld%s\n",e,s0,s1,
            (long long)chainJ(17,e,0),(long long)chainJ(342,e,0),(long long)chainJ(-342,e,0),
            e==0?"   <- baseline":"");
    }
    printf("  targets:                                       45617    18466836   18466858..67\n");
    printf("\n--- B(i)b: whole datapath, full model over 65536 angles (baseline 294) ---\n");
    P5 base; printf("  e    all-three   square-only   linear-only   cubic-only   all+halfcub\n");
    for(int e=-8;e<=8;e++){
        P5 a=base; a.e=e;
        P5 b=base; b.e=e;                       // square only: emulate by zeroing e elsewhere
        P5 c=base, d=base, g=base; g.e=e; g.halfcub=1;
        // per-term: easiest is to add dedicated fields; instead evaluate by temporarily using shw trick
        // -> implement per-term by three separate structs with e set and the others neutralised below.
        int sall=w5_score(a), sh=w5_score(g);
        printf("  %+2d   %8d   %11s   %11s   %10s   %11d%s\n",e,sall,"-","-","-",sh,e==0?"  <- baseline":"");
    }
    return 0; }
