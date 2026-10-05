// wc_jdiag.cpp -- diagnose the 96 failures of the current chain against the J windows, and print the
// required vs model asymmetry for every |t|.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 JL[1024],JH[1024]; static char OKT[1024]; static int PIN[1024];  // node that sets JL
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000; int pin=-1;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n];
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl){gl=a1;pin=node;} if(a2<gh)gh=a2; }
        JL[t+512]=gl;JH[t+512]=gh;OKT[t+512]=gl<=gh;PIN[t+512]=pin; }
    int score=0,hi_fail=0,lo_fail=0; i64 worst_hi=0,worst_lo=0;
    puts("failures of I1=(arr(KC,dx)+K)>>23 ; J=(arr(I1,dx)+K)>>20 :");
    puts("    t     JL       JH    modelJ   err    width");
    for(int t=-512;t<=511;t++){ if(!OKT[t+512])continue; i64 dx=(i64)t<<13;
        i64 I1=(arr(KC,dx)+K)>>23, J=(arr(I1,dx)+K)>>20;
        if(J>=JL[t+512]&&J<=JH[t+512]){score++;continue;}
        i64 err = J<JL[t+512] ? J-JL[t+512] : J-JH[t+512];
        if(err>0){hi_fail++; if(err>worst_hi)worst_hi=err;} else {lo_fail++; if(err<worst_lo)worst_lo=err;}
        printf("%5d %8lld %8lld %8lld %+6lld %8lld\n",t,(long long)JL[t+512],(long long)JH[t+512],(long long)J,
            (long long)err,(long long)(JH[t+512]-JL[t+512])); }
    printf("score %d/1022 ; too high %d (worst %+lld) ; too low %d (worst %+lld)\n",score,hi_fail,(long long)worst_hi,lo_fail,(long long)worst_lo);
    // asymmetry table
    puts("\n|t|   Dreq[lo,hi]  Dmodel   Jp     Jm     widths");
    for(int a=1;a<=511;a++){ int ip=a+512, im=-a+512; if(!OKT[ip]||!OKT[im])continue;
        i64 dlo=JL[im]-JH[ip], dhi=JH[im]-JL[ip];
        i64 dxp=(i64)a<<13, dxm=-dxp;
        i64 I1p=(arr(KC,dxp)+K)>>23, Jp=(arr(I1p,dxp)+K)>>20;
        i64 I1m=(arr(KC,dxm)+K)>>23, Jm=(arr(I1m,dxm)+K)>>20;
        int informative = (dlo>0)||(dhi<0)||((dhi-dlo)<=6);
        if(!informative) continue;
        int ok = (Jm-Jp)>=dlo && (Jm-Jp)<=dhi;
        printf("%4d  [%4lld,%5lld]  %4lld %s  %8lld %8lld  %lld %lld\n",a,(long long)dlo,(long long)dhi,
            (long long)(Jm-Jp), ok?"ok ":"BAD", (long long)Jp,(long long)Jm,
            (long long)(JH[ip]-JL[ip]),(long long)(JH[im]-JL[im])); }
    return 0; }
