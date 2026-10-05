// rt44_versine.cpp -- is the even term  s * (1 - cos d)  (what a rotation / sin d * tan(d/2) step produces, quartic included)
// or  s * (economised quadratic)?   J := the square-path value that multiplies the sine field, at the coarse scale (q = J/2^33).
//   A: the chain (economised KC t^2)                         B: round(2^33 (1 - cos d))  true versine, constants as fitted
//   C: true versine with the constants moved back by the Chebyshev term (C0 + round(2.029 sin)), i.e. a Taylor-centred design
//   D: pure Taylor quadratic  round(2 pi^2 t^2 / 2^32 * 2^33)  with the same un-economised constants
#include "base2.h"
#include <cmath>
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
int main(){ if(!load_fsca(F))return 1; build_tables(); long double PI=acosl(-1.0L);
    const char* nm[]={"A chain, economised quadratic, fitted constants","B true versine 1-cos d, fitted constants","C true versine, constants + Chebyshev term","D Taylor quadratic, constants + Chebyshev term"};
    for(int var=0;var<4;var++){ static i64 J[1024]; for(int t=-512;t<=511;t++){ long double d=2*PI*t/65536.0L; if(var==0){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; J[t+512]=(arr(I1,dx)+K)>>20; } else if(var==3) J[t+512]=(i64)llroundl(d*d/2*8589934592.0L); else J[t+512]=(i64)llroundl((1-cosl(d))*8589934592.0L); }
        int v=0,g=0; for(int m=1;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; const Tab&T=TB[n]; i64 s=flip?-1:1; bool gd=(m<=2||m==16||m>=30); i64 C0=C0TAB[n]+((var>=2)?(i64)llroundl(2.029L*sinl(PI*n/32)):0);
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0; i64 T2=T.S? sm(arr(J[t+512],T.S)+K,T.u2):0;
                i64 V=C0+s*T1-T2-s*T3; int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} if(V<lo||V>hi){v++; if(gd)g++;} } }
        printf("%-52s : %6d violations of 31744 (guard nodes %d)\n",nm[var],v,g); }
    return 0; }
