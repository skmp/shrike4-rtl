// wb_dev.cpp -- look at the 96 t where the current chain misses: required deviation, the fractional
// positions of both chain stages, and the exact-rational reference.  All integer (fractions printed as
// numerator/2^s).
#include "base2.h"
#include <cstring>
#include <vector>
static i64 JL[1024],JH[1024]; static int OKT[1024];
int main(int argc,char**argv){ build_tables();
    FILE*f=fopen("jwin.txt","r"); int t;long long a,b;int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKT[t+512]=o;} fclose(f);
    int show=argc>1?atoi(argv[1]):0;   // 0 = misses only, 1 = all tight windows
    puts("   t  JL-Jc JH-Jc  wid | f1=frac(arr(KC,dx)+K)/2^23  f2=frac(arr(I1,dx)+K)/2^20 | Jc-exactq  fq");
    int nmiss=0;
    for(int tt=-512;tt<=511;tt++){ if(!OKT[tt+512])continue; i64 dx=(i64)tt<<13;
        i64 P1=arr(KC,dx)+K; i64 I1=P1>>23; i64 P2=arr(I1,dx)+K; i64 Jc=P2>>20;
        i64 dl=JL[tt+512]-Jc, dh=JH[tt+512]-Jc; bool miss=(dl>0||dh<0);
        if(show==0 && !miss) continue;
        if(show==1 && (JH[tt+512]-JL[tt+512])>6 && !miss) continue;
        i64 f1=P1-(I1<<23), f2=P2-(Jc<<20);
        i64 q=(i64)KC*tt*tt>>17; i64 fq=(i64)KC*tt*tt & ((1<<17)-1);
        printf("%5d %5lld %5lld %5lld | %.6f %.6f | %4lld %.4f %s\n",tt,(long long)dl,(long long)dh,
            (long long)(JH[tt+512]-JL[tt+512]),(double)f1/8388608.0,(double)f2/1048576.0,
            (long long)(Jc-q),(double)fq/131072.0, miss?"MISS":"");
        if(miss)nmiss++; }
    printf("misses: %d\n",nmiss);
    return 0; }
