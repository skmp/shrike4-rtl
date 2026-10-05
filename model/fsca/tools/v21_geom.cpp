// v21_geom.cpp -- independent audit of the (w,s1,s2) geometry set and the KCw bracket used by w2_bf.
// Derives the bracket from scratch (no reuse of w2_bf's brk) with an EXPLICIT, over-generous slack,
// and reports both my bracket and w2_bf's, so containment can be checked.
#include "base2.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024];
static void load(){ FILE*f=fopen("jwin18.txt","r"); if(!f){printf("NO jwin18.txt\n");exit(1);} char ln[256];
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
    if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }

// ---- w2_bf's own bracket, transcribed verbatim for comparison
static const i64 LMAX=1ll<<21, KCMAX_BF=1ll<<27;
static void brk_bf(int t,int w,int s1,int s2,i64&lo,i64&hi){ long double d=fabsl((long double)t)*powl(2.0L,w); int i=t+512;
    long double sc=powl(2.0L,(long double)(s1+s2))/(d*d);
    long double slack=((long double)LMAX/powl(2.0L,s2)+2.0L)+((long double)LMAX/powl(2.0L,s1)+2.0L)*d/powl(2.0L,s2);
    long double l=((long double)JL[i]-slack)*sc,h=((long double)JH[i]+slack)*sc;
    lo=(i64)floorl(l)-8; hi=(i64)ceill(h)+8; if(lo<1)lo=1; if(hi>KCMAX_BF)hi=KCMAX_BF; }

// ---- my own bracket: 8x the loss bound, 4 ULP latch slack, 1000 extra units, no upper clamp below 2^28
static const i64 LMAX_MINE=1ll<<24;
static void brk_mine(int t,int w,int s1,int s2,i64&lo,i64&hi){ long double d=fabsl((long double)t)*powl(2.0L,w); int i=t+512;
    long double sc=powl(2.0L,(long double)(s1+s2))/(d*d);
    long double slack=((long double)LMAX_MINE/powl(2.0L,s2)+4.0L)+((long double)LMAX_MINE/powl(2.0L,s1)+4.0L)*d/powl(2.0L,s2);
    long double l=((long double)JL[i]-slack)*sc,h=((long double)JH[i]+slack)*sc;
    lo=(i64)floorl(l)-1000; hi=(i64)ceill(h)+1000; if(lo<1)lo=1; if(hi>(1ll<<28))hi=1ll<<28; }
int main(){ load();
  printf("landmarks: J(17)=[%lld,%lld]  J(342)=[%lld,%lld]  J(-342)=[%lld,%lld]\n\n",
     (long long)JL[529],(long long)JH[529],(long long)JL[854],(long long)JH[854],(long long)JL[170],(long long)JH[170]);
  printf(" w  s1  s2 |        bf_lo        bf_hi   bf_wid pass60k |      mine_lo      mine_hi  mine_wid  contains | 2^s1 pairs(bf)\n");
  long long tot_bf=0, tot_mine=0; int cfg=0;
  for(int w=6;w<=8;w++)for(int s1=12;s1<=14;s1++)for(int s2=15;s2<=16;s2++){
    long double nom=157.88L*powl(2.0L,(long double)(s1+s2-2*w));
    bool nomok = !(nom<0.5L||nom>=(long double)KCMAX_BF);
    i64 a,b; brk_bf(342,w,s1,s2,a,b); i64 ma,mb; brk_mine(342,w,s1,s2,ma,mb);
    bool p60 = !(b<a||b-a>60000);
    bool cont = (ma<=a && mb>=b);
    if(nomok&&p60){cfg++; tot_bf += (b-a+1)*((i64)1<<s1);}
    if(nomok) tot_mine += (mb-ma+1)*((i64)1<<s1);
    printf("%2d %3d %3d | %12lld %12lld %8lld %s |%12lld %12lld %9lld %9s | %5d %lld\n",
       w,s1,s2,(long long)a,(long long)b,(long long)(b-a+1), (nomok?(p60?"YES ":"no  "):"NOMX"),
       (long long)ma,(long long)mb,(long long)(mb-ma+1), cont?"yes":"NO",
       1<<s1, (long long)((b-a+1)*((i64)1<<s1)));
  }
  printf("\nw2_bf geometries accepted: %d of 18 ; (kc,k1) pairs it would enumerate: %lld\n", cfg, tot_bf);
  printf("my wider bracket would enumerate: %lld pairs\n", tot_mine);
  return 0; }
