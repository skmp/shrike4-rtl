// wb_j1.cpp -- orientation: score closed-form J(t) rules against jwin.txt, and characterise the
// residual of the required J relative to the exact rational KC*t^2/2^17.   All integer.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static int OK[1024];
static void load_jwin(){ FILE*f=fopen("jwin.txt","r"); if(!f){puts("no jwin.txt");exit(1);} int t; long long a,b; int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){ JL[t+512]=a; JH[t+512]=b; OK[t+512]=o; } fclose(f); }
struct Rule{ const char*name; i64(*f)(int); };
static i64 r_floor(int t){ return (i64)KC*t*t>>17; }
static i64 r_round(int t){ return ((i64)KC*t*t + (1<<16))>>17; }
static i64 r_ceil(int t){ return ((i64)KC*t*t + (1<<17)-1)>>17; }
static i64 r_chain(int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; }
static i64 r_chain_nok(int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return arr(I1,dx)>>20; }
static i64 r_chain_nok2(int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx))>>23; return (arr(I1,dx)+K)>>20; }
// exact-squared array form: one multiply, rec=KC, mul=t^2<<g
static i64 r_arr1(int t){ return (arr(KC,((i64)t*t)<<4)+K)>>21; }
static i64 r_arr2(int t){ return (arr(((i64)t*t)<<4,KC)+K)>>21; }
int main(){ build_tables(); load_jwin();
    Rule rs[]={{"floor(KC t^2/2^17)",r_floor},{"round",r_round},{"ceil",r_ceil},{"chain (current)",r_chain},
               {"chain, no K in 2nd",r_chain_nok},{"chain, no K in 1st",r_chain_nok2},
               {"arr(KC,t^2<<4)>>21",r_arr1},{"arr(t^2<<4,KC)>>21",r_arr2}};
    for(auto&r:rs){ int sc=0,lo=0,hi=0; for(int t=-512;t<=511;t++){ if(!OK[t+512])continue; i64 J=r.f(t);
            if(J>=JL[t+512]&&J<=JH[t+512])sc++; else if(J<JL[t+512])lo++; else hi++; }
        printf("%-24s score %4d/1022   (too low %3d, too high %3d)\n",r.name,sc,lo,hi); }
    // residual relative to exact rational: required interval in units of 2^-17 of KC t^2
    puts("\n-- required J minus KC*t^2/2^17 (exact rational, as floating display of integer numerators) --");
    puts("  t : [JL-q , JH-q]  where q = floor(KC t^2 / 2^17), frac = (KC t^2) mod 2^17 / 2^17");
    int nprint=0;
    for(int a=1;a<=511;a++){ int ip=a+512, im=-a+512; if(!OK[ip]||!OK[im])continue;
        i64 q=(i64)KC*a*a>>17; i64 fr=(i64)KC*a*a & ((1<<17)-1);
        i64 pl=JL[ip]-q, ph=JH[ip]-q, ml=JL[im]-q, mh=JH[im]-q;
        bool disj = ph<ml || mh<pl;
        if(disj||a%64==0){ printf("%4d: +[%4lld,%4lld] -[%4lld,%4lld] frac=%.4f %s\n",a,(long long)pl,(long long)ph,
            (long long)ml,(long long)mh,(double)fr/131072.0, disj?"DISJOINT":""); if(++nprint>60)break; } }
    // how many t does the symmetric rational rule satisfy on each side
    int both=0,disj=0; for(int a=1;a<=511;a++){ int ip=a+512,im=-a+512; if(!OK[ip]||!OK[im])continue;
        if(JH[ip]<JL[im]||JH[im]<JL[ip])disj++; else both++; }
    printf("\n|t| with intersecting +/- windows: %d, disjoint: %d  -> symmetric-rule ceiling = %d\n",both,disj,1022-2*disj);
    return 0; }
