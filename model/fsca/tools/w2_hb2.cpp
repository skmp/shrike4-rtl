// w2_hb2.cpp -- HOW CLOSE does each operand-formation variant get?  Over every alignment w in [2,24],
// both shifts in [4,40] and every stored constant KCw in the rigorous bracket, take the configurations
// that make J(342) exact and report the smallest |J(17) - 45617| any of them achieves.  The t=17
// window has width 1, so this is the margin by which the hypothesis fails at its best.
#include "base2.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024];
static const i64 LMAX=1ll<<21, KCMAX=1ll<<28;
static void brkd(int t,long double d,int s1,int s2,i64&lo,i64&hi){ int i=t+512;
    long double sc=powl(2.0L,(long double)(s1+s2))/(d*d);
    long double slack=((long double)LMAX/powl(2.0L,s2)+2.0L)+((long double)LMAX/powl(2.0L,s1)+2.0L)*d/powl(2.0L,s2);
    long double l=((long double)JL[i]-slack)*sc,h=((long double)JH[i]+slack)*sc;
    lo=(i64)floorl(l)-12; hi=(i64)ceill(h)+12; if(lo<1)lo=1; if(hi>KCMAX)hi=KCMAX; }
int main(){
    { FILE*f=fopen("jwin18.txt","r"); char ln[256];
      while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
    struct V{const char*n; int pnum,pden; int mag;};
    V vs[]={{"baseline  t_eff = t",0,0,0},
            {"t + 1/2 (one extra bit of precision)",1,1,0},
            {"t - 1/2",-1,1,0},
            {"|t| + 1/2 (magnitude, away from zero)",1,1,1},
            {"|t| - 1/2 (magnitude, toward zero)",-1,1,1},
            {"t + 1/4",1,2,0},{"t - 1/4",-1,2,0},
            {"t + 1 (whole ULP)",1,0,0},{"t - 1",-1,0,0}};
    printf("%-38s | cfgs J(342) exact | min |J(17)-45617| | at (w,s1,s2,KCw)\n","variant");
    for(auto&v:vs){
        long long nhit=0; i64 bestd=1ll<<60; int bw=0,bs1=0,bs2=0; i64 bkc=0,bj=0;
        #pragma omp parallel for schedule(dynamic) collapse(2) reduction(+:nhit)
        for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++){
          for(int s2=4;s2<=40;s2++){
            long double nom=157.88L*powl(2.0L,(long double)(s1+s2-2*w)); if(nom<0.5L||nom>=(long double)KCMAX) continue;
            int sh=w-v.pden; i64 p = sh>=0 ? (i64)v.pnum<<sh : (i64)v.pnum>>(-sh);
            auto DX=[&](int t)->i64{ i64 d=(i64)t<<w; return d + (v.mag? (t<0?-p:p) : p); };
            long double dp=fabsl((long double)DX(342)); if(dp<1) continue;
            i64 lo,hi; brkd(342,dp,s1,s2,lo,hi); if(hi<lo) continue;
            i64 locd=1ll<<60,lockc=0,locj=0;
            for(i64 kc=lo;kc<=hi;kc++){
                i64 dxp=DX(342); i64 jp=(arr((arr(kc,dxp)+K)>>s1,dxp)+K)>>s2;
                if(jp<JL[854]||jp>JH[854]) continue; nhit++;
                i64 dx17=DX(17); i64 j17=(arr((arr(kc,dx17)+K)>>s1,dx17)+K)>>s2;
                i64 d=llabs(j17-JL[529]); if(d<locd){locd=d;lockc=kc;locj=j17;} }
            if(locd<(1ll<<60)){
              #pragma omp critical
              { if(locd<bestd){bestd=locd;bw=w;bs1=s1;bs2=s2;bkc=lockc;bj=locj;} } }
          } }
        if(nhit) printf("%-38s | %17lld | %17lld | (%d,%d,%d,%lld) J(17)=%lld\n",v.n,nhit,(long long)bestd,bw,bs1,bs2,(long long)bkc,(long long)bj);
        else     printf("%-38s | %17lld | %17s |\n",v.n,nhit,"n/a");
        fflush(stdout);
      }
    return 0; }
