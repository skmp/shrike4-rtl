// wa_asym.cpp -- the array's own +t/-t asymmetry, in J units, for every candidate product,
// compared with the asymmetry the data REQUIRES.
#include "wa_common.h"
static Windows W;
int main(){ build_tables(); if(!load_windows(W))return 1;
    puts("|t| Dreq_lo Dreq_hi | dxdx_asym  (in J units at s=26,20) | KCdx_asym (J units) | I1dx_asym");
    int show[]={342,376,40,258,411,447,277,469,251,348,303,408,144,86,148,179,233,288,301,324,431,499};
    for(int a : show){ int ip=a+512,im=-a+512; if(!W.ok[ip]||!W.ok[im])continue;
        i64 dlo=W.JL[im]-W.JH[ip], dhi=W.JH[im]-W.JL[ip];
        i64 dp=(i64)a<<13, dm=-dp;
        i64 sq_p=arr(dp,dp), sq_m=arr(dm,dm);          // dx^2 both signs
        i64 kc_p=arr(KC,dp), kc_m=arr(KC,dm);
        i64 I1p=(kc_p+K)>>23, I1m=(kc_m+K)>>23;
        i64 p2p=arr(I1p,dp), p2m=arr(I1m,dm);
        // asymmetries expressed as J units: dJ = dP2/2^20 ; for the square path dJ = (asym/2^26)*KC/2^17*...
        printf("%4d  %5lld %5lld | dx2: %+9lld (%.3f J)  | KCdx: %+9lld (%.4f I1lsb, %.3f J) | I1dx: %+8lld (%.3f J)\n",
            a,(long long)dlo,(long long)dhi,
            (long long)(sq_m-sq_p), (double)(sq_m-sq_p)/67108864.0*39.4712,
            (long long)(-(kc_p+kc_m)), (double)(-(kc_p+kc_m))/8388608.0, (double)(-(kc_p+kc_m))/8388608.0*a/128.0,
            (long long)(p2m-p2p), (double)(p2m-p2p)/1048576.0); }
    // full statistics
    i64 mx=0; int amx=0; double mxj=0;
    for(int a=1;a<=512;a++){ i64 dp=(i64)a<<13; i64 v=-(arr(KC,dp)+arr(KC,-dp)); if(v>mx){mx=v;amx=a;} }
    printf("\nmax |arr(KC,dx)+arr(KC,-dx)| over t: %lld at |t|=%d  = %.4f LSB of a 2^23 latch\n",(long long)mx,amx,(double)mx/8388608.0);
    for(int a=1;a<=512;a++){ i64 dp=(i64)a<<13; i64 v=arr(-dp,-dp)-arr(dp,dp); double j=(double)v/67108864.0*39.4712;
        if(fabs(j)>mxj){mxj=fabs(j);amx=a;} }
    printf("max |arr(dx,dx)-arr(-dx,-dx)| in J units: %.4f at |t|=%d\n",mxj,amx);
    return 0; }
