// wb_gran.cpp -- how WIDE must the square-path latch be?  J (the latch at scale 2^-33 of the fraction
// 1 - 2pi^2 dx^2) must be a multiple of 2^(33-W) if the physical latch holds W fraction bits.
// Count feasible t whose allowed J window contains such a multiple.  Also the widest window and the
// tightest t for each W.
#include "base2.h"
#include <cstring>
static i64 JL[1024],JH[1024]; static int OKT[1024];
int main(){ FILE*f=fopen("jwin.txt","r"); if(!f){puts("no jwin.txt");return 1;} int t; long long a,b; int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){ JL[t+512]=a; JH[t+512]=b; OKT[t+512]=o; } fclose(f);
    puts(" W  step   t satisfiable  first failing t (window)");
    for(int W=20;W<=33;W++){ i64 step=1ll<<(33-W); int good=0; int ft=999; i64 fl=0,fh=0;
        for(int tt=-512;tt<=511;tt++){ if(!OKT[tt+512])continue; i64 lo=JL[tt+512],hi=JH[tt+512];
            i64 m=((lo+step-1)/step)*step;              // smallest multiple >= lo
            if(m<=hi) good++; else if(ft==999){ft=tt;fl=lo;fh=hi;} }
        printf("%2d %6lld  %5d/1022      %s", W,(long long)step,good, ft==999?"-- all ok":"");
        if(ft!=999) printf("t=%d [%lld,%lld]",ft,(long long)fl,(long long)fh);
        printf("\n"); }
    return 0; }
