// n6_tround.cpp -- the literal reading: t itself ROUNDED rather than truncated, before the square path.
#include "base2.h"
#include <cstdio>
static i64 JL[1024],JH[1024]; static bool have[1024];
static i64 J_of(int te){ i64 dx=(i64)te<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>18; }
static int sc(int mode,int k){ int s=0;
    for(int i=0;i<1024;i++) if(have[i]){ int t=i-512,te;
        if(k==0) te=t;
        else if(mode==0) te=(t>>k)<<k;                       // truncate (floor)
        else if(mode==1) te=((t+(1<<(k-1)))>>k)<<k;          // round to nearest
        else te = t>=0 ? ((t>>k)<<k) : -(((-t)>>k)<<k);      // truncate toward zero
        i64 j=J_of(te); if(j>=JL[i]&&j<=JH[i]) s++; }
    return s; }
int main(){ FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} }
    fclose(f);
    printf("t reduced to fewer bits before the SQUARE PATH only (linear/cubic keep full t):\n");
    printf("  k   floor   round-to-nearest   truncate-toward-0     (k=0 is the model: %d/1024)\n",sc(0,0));
    for(int k=1;k<=6;k++) printf("  %d  %5d   %14d   %17d\n",k,sc(0,k),sc(1,k),sc(2,k));
    return 0; }
