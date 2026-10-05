// wd_lambda.cpp -- is the C0 table a rounded/truncated sine of a SCALED angle?
// For each node the t=0 window gives an interval for sin(lambda*n*pi/32)*2^26; invert it to an
// interval for lambda by bisection in Q60 integer arithmetic and intersect over the nodes.
#include "base2.h"
#include <cstring>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{hi=-w.lo;lo=-w.hi;}ex=w.exact;}
// sin(lam * n*pi/32) * 2^26 in Q60*2^26 -> return value scaled by 2^26, as i128 in Q60
static i128 sval(i128 lam /*Q60*/, int n){ i128 a = PI_()*n/32; a = a*lam/QONE; return sin_(a)*((i128)1<<26); }
int main(){ if(!load_fsca(F))return 1; build_tables();
    // per node window of the internal value at t=0 (both node n and 32-n must agree)
    i64 LO[17],HI[17];
    for(int n=0;n<=16;n++){ i64 l1,h1,l2,h2; bool e;
        win_of(n*1024,l1,h1,e); win_of(((32-n)*1024)&0xffff,l2,h2,e);
        LO[n]=std::max(l1,l2); HI[n]=std::min(h1,h2);
        printf("n=%2d window [%lld,%lld]\n",n,(long long)LO[n],(long long)HI[n]); }
    const char* fn[3]={"floor","RN","ceil"};
    for(int f=0;f<3;f++){
        // allowed real value V of sin*2^26:  floor: [LO, HI+1) ; RN: [LO-0.5, HI+0.5) ; ceil: (LO-1, HI]
        i128 gl = (i128)((long double)QONE*0.99990L), gh = (i128)((long double)QONE*1.00010L);
        printf("-- %s --\n",fn[f]);
        bool empty=false;
        for(int n=1;n<=16;n++){
            // target real interval [A,B) scaled by 2^60
            i128 A,B;
            if(f==0){ A=(i128)LO[n]*QONE; B=(i128)(HI[n]+1)*QONE; }
            else if(f==1){ A=(i128)LO[n]*QONE-QONE/2; B=(i128)HI[n]*QONE+QONE/2; }
            else { A=(i128)(LO[n]-1)*QONE+1; B=(i128)HI[n]*QONE+1; }
            if(n==16){ printf("   n=16 (sin=1, clamped) skipped\n"); continue; }
            // sval is increasing in lam on this range -> bisect
            i128 lo=(i128)((long double)QONE*0.9900L), hi=(i128)((long double)QONE*1.0100L);
            i128 a1=lo,b1=hi; for(int it=0;it<200;it++){ i128 m=(a1+b1)/2; if(sval(m,n)>=A) b1=m; else a1=m+1; }
            i128 a2=lo,b2=hi; for(int it=0;it<200;it++){ i128 m=(a2+b2+1)/2; if(sval(m,n)<B) a2=m; else b2=m-1; }
            long double L1=(long double)(double)(long long)(a1-QONE)/(long double)QONE, L2=(long double)(double)(long long)(a2-QONE)/(long double)QONE;
            printf("   n=%2d lambda in [1%+.3Le, 1%+.3Le]\n",n,L1,L2);
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; if(gl>gh){empty=true;} }
        if(empty) printf("   => EMPTY intersection\n");
        else printf("   => lambda in [1%+.4Le, 1%+.4Le]  NON-EMPTY\n",
            (long double)(double)(long long)(gl-QONE)/(long double)QONE,(long double)(double)(long long)(gh-QONE)/(long double)QONE);
    }
    // what lambda would C1's pi imply?  2*C1f[0] = pi/2 * 2^23 field
    printf("\nC1-implied pi field: 2*C1f[0]=%lld  (true pi/2*2^23 = 13176794.633)\n",(long long)(2*TB[0].C1f));
    return 0; }
