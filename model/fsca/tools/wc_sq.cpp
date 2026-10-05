// wc_sq.cpp -- is the squaring array arr(dx,dx) itself sign-asymmetric?  If so it is a candidate
// source for the non-monotone asymmetry of the square latch.
#include "base2.h"
#include <cstring>
int main(){ build_tables();
    // A(t) = arr(dx,dx); exact square is t^2 * 2^26
    int nz=0, asym=0; i64 mind=1ll<<62, maxd=-(1ll<<62);
    for(int t=0;t<=512;t++){ i64 dp=(i64)t<<13, dm=-dp;
        i64 Ap=arr(dp,dp), Am=arr(dm,dm), want=(i64)t*t<<26;
        if(Ap!=want||Am!=want) nz++;
        if(Ap!=Am){ asym++; i64 d=Am-Ap; if(d<mind)mind=d; if(d>maxd)maxd=d;
            if(asym<=25) printf("t=%4d  A(+)=%lld off %+lld   A(-)=%lld off %+lld   diff %+lld\n",t,
                (long long)Ap,(long long)(Ap-want),(long long)Am,(long long)(Am-want),(long long)(Am-Ap)); } }
    printf("arr(dx,dx) != exact: %d/513 ; asymmetric A(-t)!=A(+t): %d/513, diff range [%lld,%lld]\n",
        nz,asym,(long long)mind,(long long)maxd);
    // distribution of the deficit
    int h[8]={0}; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 d=((i64)t*t<<26)-arr(dx,dx);
        int b = d==0?0 : (d==(1ll<<17)?1 : (d==(1ll<<18)?2 : (d==(1ll<<17)+(1ll<<18)?3:4))); h[b]++; }
    printf("deficit histogram over t=-512..511: 0:%d 2^17:%d 2^18:%d 3*2^17:%d other:%d\n",h[0],h[1],h[2],h[3],h[4]);
    // also: mixed-sign square arr(dx,-dx)
    int mix=0; for(int t=1;t<=512;t++){ i64 dp=(i64)t<<13; if(arr(dp,-dp)!=-arr(dp,dp)) mix++; }
    printf("arr(dx,-dx) != -arr(dx,dx): %d/512\n",mix);
    // is arr(a,-b) == -arr(a,b) in general for the coefficient*dx case?
    int lin=0; for(int t=1;t<=512;t++){ i64 dx=(i64)t<<13; if(arr(0x4ef145,-dx)!=-arr(0x4ef145,dx)) lin++; }
    printf("arr(KC,-dx) != -arr(KC,dx): %d/512\n",lin);
    return 0; }
