// w2_hb.cpp -- quantify the user's "extra bit of precision" hypothesis: re-fit the stored constant so
// that t=342 is exact, then read off t=17.  A half-ULP on t adds a term LINEAR in t to the square,
// and a stored constant can only rescale the QUADRATIC term, so the two offsets cannot both be served.
#include "base2.h"
#include <cstdio>
#include <cmath>
static i64 JL[1024],JH[1024]; static bool have[1024];
int main(){
    { FILE*f=fopen("jwin18.txt","r"); char ln[256];
      while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
    const int w=13,s1=23,s2=18;
    printf("geometry w=13 s1=23 s2=18, stored constant refitted per variant so that J(342) is exact\n");
    printf("%-34s  KCw        J(342)      J(17)    need 45617   err    full/1024\n","variant");
    struct V{const char*n; i64 p; int qsign;};
    V vs[]={{"none (baseline)",0,0},{"t + 1/2 (half ULP up)",4096,0},{"t - 1/2",-4096,0},
            {"|t| + 1/2 (magnitude, away from 0)",4096,1},{"|t| - 1/2",-4096,-1},
            {"t + 1/4",2048,0},{"t - 1/4",-2048,0},{"t + 1/8",1024,0}};
    for(auto&v:vs){
        auto J=[&](int t,i64 kc)->i64{ i64 dx=((i64)t<<w)+v.p*(v.qsign? (t<0?-1:1):1);
            i64 I1=(arr(kc,dx)+K)>>s1; return (arr(I1,dx)+K)>>s2; };
        // find the KCw that puts t=342 in window (binary search on a monotone quantity, then scan +-64)
        i64 lo=1,hi=1ll<<28;
        while(lo<hi){ i64 m=lo+(hi-lo)/2; if(J(342,m)>=JL[854]) hi=m; else lo=m+1; }
        i64 kc=-1; for(i64 c=lo-64;c<=lo+64;c++){ if(c<1)continue; i64 j=J(342,c); if(j>=JL[854]&&j<=JH[854]){kc=c;break;} }
        if(kc<0){ printf("%-34s  (no constant puts t=342 in window)\n",v.n); continue; }
        int sc=0; for(int i=0;i<1024;i++) if(have[i]){ i64 j=J(i-512,kc); if(j>=JL[i]&&j<=JH[i])sc++; }
        printf("%-34s  %-9lld  %-10lld  %-8lld %-12lld %+7lld  %d\n",v.n,(long long)kc,(long long)J(342,kc),
            (long long)J(17,kc),(long long)JL[529],(long long)(J(17,kc)-JL[529]),sc);
    }
    return 0; }
