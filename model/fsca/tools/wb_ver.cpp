// wb_ver.cpp -- direct (non-interval) verification of the chain scores this session's search reported.
#include "base2.h"
#include <cstring>
static i64 JL[1024],JH[1024]; static int OKT[1024];
int main(){ build_tables();
    FILE*f=fopen("jwin.txt","r"); int t;long long a,b;int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKT[t+512]=o;} fclose(f);
    struct C{const char*n; i64 A,K1,K2; int s1,s2,r1,r2,ro1,ro2;};
    C cs[]={
      {"chain KC s1=23 s2=20 K1=2^19 K2=2^19 (baseline)",5173573,524288,524288,23,20,0,0,0,0},
      {"chain KC s1=23 s2=20 K1=2^19 K2=0",5173573,524288,0,23,20,0,0,0,0},
      {"s1=25 s2=20 A=20694294 K1=-3276800 sm2",20694294,-3276800,0,25,20,0,1,0,0},
      {"s1=27 s2=20 A=82777175 K1=-15859712 sm2 ro1=1",82777175,-15859712,0,27,20,0,1,1,0},
      {"s1=24 s2=20 A=10347147 K1=-1703936 ro1=1",10347147,-1703936,0,24,20,0,0,1,0},
      {"s1=26 s2=20 A=41388588 K1=-6488064 sm2",41388588,-6488064,0,26,20,0,1,0,0},
    };
    for(auto&c:cs){ int sc=0;
        for(int tt=-512;tt<=511;tt++){ if(!OKT[tt+512])continue; i64 dx=(i64)tt<<13;
            i64 P1=c.ro1?arr(dx,c.A):arr(c.A,dx); i64 L=c.r1? sm(P1+c.K1,c.s1):((P1+c.K1)>>c.s1);
            i64 P2=c.ro2?arr(dx,L):arr(L,dx); i64 J=c.r2? sm(P2+c.K2,c.s2):((P2+c.K2)>>c.s2);
            if(J>=JL[tt+512]&&J<=JH[tt+512])sc++; }
        printf("%-48s : %4d/1022\n",c.n,sc); }
    // symmetric single multiply
    int sc=0; for(int tt=-512;tt<=511;tt++){ if(!OKT[tt+512])continue;
        i64 J=arr(((i64)tt*tt)<<1,2586787)>>17; if(J>=JL[tt+512]&&J<=JH[tt+512])sc++; }
    printf("%-48s : %4d/1022\n","symmetric arr(2t^2,2586787)>>17",sc);
    return 0; }
