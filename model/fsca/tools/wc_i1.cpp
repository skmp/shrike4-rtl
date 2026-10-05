// wc_i1.cpp -- invert the SECOND multiply of the square chain: which integers I1 put J into its window?
// Then compare the required I1 with the exact real value KC*t*2^13/2^23 to read off the first latch's rule.
// Scans I1 in a +-4000 window around the model value (J moves by |t|/128 per I1 step, so this is ample).
#include "wc_core.h"
#include <vector>
#include <algorithm>
#include <cmath>
static i64 JLo[1024],JHi[1024]; static char JOK[1024];
static void build_jwin(){
    for(int t=-512;t<=511;t++){ i64 gl=0,gh=12000000;
        for(int node=1;node<=31;node++){ int n=node>=17?32-node:node; if(!TB[n].S)continue; const Tab&T=TB[n];
            i64 base=C0B[n]+LT[node][t+512];
            i64 t2lo=base-WHI[node][t+512], t2hi=base-WLO[node][t+512]; int u=T.u2;
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JLo[t+512]=gl;JHi[t+512]=gh;JOK[t+512]=gl<=gh; } }
static i64 ILO[1024], IHI[1024]; static char IOK[1024];
int main(){ wc_init(); build_jwin();
    #pragma omp parallel for schedule(dynamic)
    for(int t=-512;t<=511;t++){ IOK[t+512]=0; if(!JOK[t+512]) continue; i64 dx=(i64)t<<13;
        i64 I1m=(arr(KC,dx)+K)>>23; i64 lo=1,hi=0; int first=1;
        for(i64 i1=I1m-4000;i1<=I1m+4000;i1++){ i64 J=(arr(i1,dx)+K)>>20;
            if(J>=JLo[t+512]&&J<=JHi[t+512]){ if(first){lo=i1;first=0;} hi=i1; } }
        if(!first){ ILO[t+512]=lo; IHI[t+512]=hi; IOK[t+512]=1; } }
    puts(" t    I1req_lo  I1req_hi   I1model   real=KC*dx/2^23        frac    dev   width");
    int nbad=0,nmiss=0,w0=0,w1=0,w2=0,wb=0;
    for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue;
        if(!IOK[t+512]){ nmiss++; printf("%5d   (no integer I1 reaches the J window)\n",t); continue; }
        i64 dx=(i64)t<<13; i64 I1m=(arr(KC,dx)+K)>>23;
        long double real=(long double)KC*(long double)dx/(long double)(1ll<<23);
        long double fr=real-floorl(real);
        i64 lo=ILO[t+512],hi=IHI[t+512]; i64 w=hi-lo;
        if(w==0)w0++; else if(w==1)w1++; else if(w==2)w2++; else wb++;
        int dev = I1m<lo ? (int)(I1m-lo) : (I1m>hi ? (int)(I1m-hi) : 0);
        if(dev){ nbad++;
            printf("%5d %10lld %10lld %10lld %18.4Lf  %7.4Lf   %+3d  %4lld\n",t,(long long)lo,(long long)hi,
                (long long)I1m,real,fr,dev,(long long)w); } }
    printf("\nno feasible I1: %d ; model I1 outside: %d ; widths 0:%d 1:%d 2:%d >2:%d\n",nmiss,nbad,w0,w1,w2,wb);
    // does the required I1 interval always contain floor(real)?  ceil?  round?
    int cfl=0,cce=0,crn=0,ctz=0,n=0;
    for(int t=-512;t<=511;t++){ if(!IOK[t+512])continue; n++;
        i64 dx=(i64)t<<13; long double real=(long double)KC*(long double)dx/(long double)(1ll<<23);
        i64 fl=(i64)floorl(real), ce=(i64)ceill(real), rn=(i64)llroundl(real), tz=(i64)(real<0?ceill(real):floorl(real));
        if(fl>=ILO[t+512]&&fl<=IHI[t+512])cfl++;
        if(ce>=ILO[t+512]&&ce<=IHI[t+512])cce++;
        if(rn>=ILO[t+512]&&rn<=IHI[t+512])crn++;
        if(tz>=ILO[t+512]&&tz<=IHI[t+512])ctz++; }
    printf("required interval contains: floor(real) %d/%d  ceil %d  round %d  trunc-to-zero %d\n",cfl,n,cce,crn,ctz);
    // best constant offset added to the real value before flooring
    int bestc=0; long double bestd=0;
    for(int k=-400;k<=400;k++){ long double off=k/100.0L; int c=0;
        for(int t=-512;t<=511;t++){ if(!IOK[t+512])continue;
            i64 dx=(i64)t<<13; long double real=(long double)KC*(long double)dx/(long double)(1ll<<23)+off;
            i64 v=(i64)floorl(real); if(v>=ILO[t+512]&&v<=IHI[t+512])c++; }
        if(c>bestc){bestc=c;bestd=off;} }
    printf("best  floor(KC*dx/2^23 + off)  : %d/%d at off=%.2Lf\n",bestc,n,bestd);
    // and with a scaled constant
    int bestc2=0; long double bA=0,bO=0;
    #pragma omp parallel
    { int lc=0; long double lA=0,lO=0;
      #pragma omp for schedule(static)
      for(int dk=-600;dk<=600;dk++){ long double A=(long double)KC+dk/100.0L;
        for(int k=-300;k<=300;k++){ long double off=k/50.0L; int c=0;
          for(int t=-512;t<=511;t++){ if(!IOK[t+512])continue;
            long double real=A*(long double)((i64)t<<13)/(long double)(1ll<<23)+off;
            i64 v=(i64)floorl(real); if(v>=ILO[t+512]&&v<=IHI[t+512])c++; }
          if(c>lc){lc=c;lA=A;lO=off;} } }
      #pragma omp critical
      if(lc>bestc2){bestc2=lc;bA=lA;bO=lO;} }
    printf("best  floor(A*dx/2^23 + off)   : %d/%d at A=%.2Lf off=%.2Lf\n",bestc2,n,bA,bO);
    return 0; }
