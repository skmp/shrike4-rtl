// wc_res.cpp -- what is the residual of the required square latch J beyond the best pure quadratic?
// Restrict to the t whose J window is TIGHT (width <= 4) so the required value is essentially pinned,
// and print candidate explanatory quantities next to the residual.
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
int main(){ wc_init(); build_jwin();
    const long double A=662217436.0L/16777216.0L, R=-14436800.0L/16777216.0L;   // wc_fit joint best
    puts("  t   JL      JH    w   quad     resid_lo resid_hi | frac(KC|t|/2^10)  arrloss/2^16  t&31  chainJ-quad");
    int n=0;
    for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue; i64 w=JHi[t+512]-JLo[t+512]; if(w>4) continue;
        long double q=A*(long double)t*t+R;
        i64 dx=(i64)t<<13;
        long double fr=(long double)KC*(long double)(t<0?-t:t)/1024.0L; fr-=floorl(fr);
        i64 loss=(i64)KC*dx-arr(KC,dx);
        i64 I1=(arr(KC,dx)+K)>>23, cJ=(arr(I1,dx)+K)>>20;
        printf("%5d %9lld %9lld %2lld %10.2Lf  %+8.2Lf %+8.2Lf |  %6.4Lf  %8.2f  %3d  %+8.2Lf\n",
            t,(long long)JLo[t+512],(long long)JHi[t+512],(long long)w,q,
            JLo[t+512]-q, JHi[t+512]-q, fr, loss/65536.0, t&31, cJ-q);
        if(++n>70) break; }
    // correlation test: does resid correlate with frac?
    long double s1=0,s2=0,s11=0,s22=0,s12=0; int m=0;
    for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue; if(JHi[t+512]-JLo[t+512]>4) continue;
        long double q=A*(long double)t*t+R; long double r=((long double)JLo[t+512]+JHi[t+512])/2-q;
        long double fr=(long double)KC*(long double)(t<0?-t:t)/1024.0L; fr-=floorl(fr);
        s1+=r; s2+=fr; s11+=r*r; s22+=fr*fr; s12+=r*fr; m++; }
    long double cv=(s12-s1*s2/m)/sqrtl((s11-s1*s1/m)*(s22-s2*s2/m));
    printf("\ntight-window samples: %d ; corr(resid, frac(KC|t|/2^10)) = %.4Lf\n",m,cv);
    return 0; }
