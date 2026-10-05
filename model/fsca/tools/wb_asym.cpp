// wb_asym.cpp -- the required sign asymmetry D(|t|) = J(-t) - J(t), as an exact interval, for every |t|,
// and how it compares with one LSB of a first-stage latch (q = |t| * 2^(s1-30)).
#include "base2.h"
#include <cstring>
#include <vector>
static i64 JL[1024],JH[1024]; static int OKT[1024];
int main(int argc,char**argv){ FILE*f=fopen(argc>1?argv[1]:"jwin.txt","r"); if(!f){puts("no jwin");return 1;}
    int t; long long a,b; int o; while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKT[t+512]=o;} fclose(f);
    int forced=0, zeroable=0;
    puts("|t|  Dlo  Dhi   t/256  t/128   t/64   t/32   consistent-with (s1 such that D range contains floor/ceil(t*2^(s1-30)))");
    for(int A=1;A<=511;A++){ int ip=A+512, im=-A+512; if(!OKT[ip]||!OKT[im])continue;
        i64 dlo=JL[im]-JH[ip], dhi=JH[im]-JL[ip];
        if(dlo<=0&&0<=dhi) zeroable++;
        if(dlo>0){ forced++;
            printf("%4d %4lld %4lld  %6.2f %6.2f %6.2f %6.2f  ",A,(long long)dlo,(long long)dhi,A/256.0,A/128.0,A/64.0,A/32.0);
            for(int s1=20;s1<=28;s1++){ long double q=(long double)A*powl(2.0L,(long double)s1-30.0L);
                i64 fq=(i64)floorl(q), cq=(i64)ceill(q);
                if((fq>=dlo&&fq<=dhi)||(cq>=dlo&&cq<=dhi)) printf("s1=%d ",s1); }
            printf("\n"); } }
    printf("\n|t| with both windows non-empty: %d ; D forced > 0 at %d of them; D=0 allowed at %d\n",
        forced+zeroable,forced,zeroable);
    // how often is D=0 FORCED (i.e. dlo<=0<=dhi and width small)
    int tight0=0; for(int A=1;A<=511;A++){int ip=A+512,im=-A+512; if(!OKT[ip]||!OKT[im])continue;
        i64 dlo=JL[im]-JH[ip], dhi=JH[im]-JL[ip]; if(dlo<=0&&dhi>=0&&dhi-dlo<=3) tight0++; }
    printf("|t| where D is pinned to a window of width<=3 containing 0: %d\n",tight0);
    return 0; }
