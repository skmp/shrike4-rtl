// rt43_shear.cpp -- the three-shear rotation  s' = s + sin(d) * (c - tan(d/2) * s)  in its MOST FAVOURABLE form:
//   sin(d)   := the chip's own node-0 output at offset t (exact integer on the 2^-26 grid)   [so "the polynomial in d" is right by construction]
//   tan(d/2) := exact real, or sin(d)/2 * (1 + d^2/12) etc.;  s, c := the chip's own centre values (C0 of words n and 16-n)
//   inner and outer products rounded by floor / nearest / ceil / toward zero (16 combinations), the inner one on grids 2^-26..2^-32.
// Scored on the EXACT-valued nodes 1 and 2 (+ mirrors 30, 31): 4096 samples with width-1 windows.  The polynomial model: 0.
#include "base2.h"
#include <cmath>
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static long double rnd(long double x,int mode){ switch(mode){case 0:return floorl(x);case 1:return floorl(x+0.5L);case 2:return ceill(x);default:return x<0?ceill(x):floorl(x);} }
int main(){ if(!load_fsca(F))return 1; long double PI=acosl(-1.0L); int best=1<<30; int bcfg[3]={0,0,0};
    static i64 SD[1024]; for(int t=-512;t<=511;t++){ int a=t&0xffff; Win w=decode(F.srz[a],F.srn[a]); SD[t+512]=w.sign*w.lo; }
    for(int g=0;g<=6;g++) for(int mi=0;mi<4;mi++) for(int mo=0;mo<4;mo++){ int miss=0; long double G=powl(2,g);
        for(int n=1;n<=2;n++) for(int i=0;i<2;i++){ int m=i?32-n:n; long double s=C0TAB[n], c=C0TAB[16-n]*(i?-1.0L:1.0L);
            for(int t=-512;t<=511;t++){ long double d=2*PI*t/65536.0L, td=tanl(d/2);
                long double X=c-rnd(td*s*G,mi)/G;                       // c - tan(d/2) s, inner product on the 2^-(26+g) grid
                long double V=s+rnd((long double)SD[t+512]*X/67108864.0L,mo);
                int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 hw=w.sign*w.lo; if((i64)V!=hw)miss++; } }
        if(miss<best){best=miss;bcfg[0]=g;bcfg[1]=mi;bcfg[2]=mo;} }
    printf("three-shear rotation, best of 112 rounding/grid choices: %d mismatches of 4096 exact samples (inner grid 2^-%d, inner rounding %d, outer %d)\n",best,26+bcfg[0],bcfg[1],bcfg[2]);
    // how far off, typically?
    { int hist[7]={0}; long double G=powl(2,bcfg[0]); for(int n=1;n<=2;n++) for(int i=0;i<2;i++){ int m=i?32-n:n; long double s=C0TAB[n], c=C0TAB[16-n]*(i?-1.0L:1.0L); for(int t=-512;t<=511;t++){ long double d=2*PI*t/65536.0L, td=tanl(d/2); long double X=c-rnd(td*s*G,bcfg[1])/G; long double V=s+rnd((long double)SD[t+512]*X/67108864.0L,bcfg[2]); int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 df=(i64)V-w.sign*w.lo; if(df<-3)df=-3; if(df>3)df=3; hist[df+3]++; } }
      printf("   rotation - hardware:  <=-3:%d  -2:%d  -1:%d  0:%d  +1:%d  +2:%d  >=+3:%d\n",hist[0],hist[1],hist[2],hist[3],hist[4],hist[5],hist[6]); }
    return 0; }
