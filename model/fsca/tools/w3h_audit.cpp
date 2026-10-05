// w3h_audit.cpp -- what the hardware loss probe does and does not pin about the array's DIGIT COUNT,
// and which square-path calls are even sensitive to it.
#include "w3_common.h"
int main(){ if(!load_fsca(FD))return 1; build_tables();
    // 1) how many of the square path's array calls carry a recoded operand needing >13 Booth digits?
    int n1=0,n2=0,n3=0, mx=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        i64 I1=(arr(KC,dx)+K)>>23, J=(arr(I1,dx)+K)>>18;
        auto dig=[](i64 v){ u64 a=(u64)(v<0?-v:v); int b=a? 64-__builtin_clzll(a):0; return b/2+1; };
        if(dig(KC)>13)n1++; if(dig(I1)>13)n2++; if(dig(J)>13){n3++; if(dig(J)>mx)mx=dig(J);} }
    printf("square-path recoded operands needing more than 13 Booth digits (of 1024 offsets each):\n");
    printf("  arr(KC,dx): %d   arr(I1,dx): %d   arr(J,S): %d  (max %d digits)\n", n1,n2,n3,mx);
    // 2) is ND >= 14 ever distinguishable at all for these operands?
    int diff=0; for(int nd=14;nd<=18;nd++){ ND_T1=ND_T3a=ND_T3b=ND_SQ1=ND_SQ2=ND_T2=nd;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 a=(arrN(KC,dx,nd)+K)>>23, b=(arrN(KC,dx,14)+K)>>23; if(a!=b)diff++;
            i64 c=(arrN(a,dx,nd)+K)>>18, d=(arrN(b,dx,14)+K)>>18; if(c!=d)diff++; } }
    printf("  ND in 14..18 vs ND=14, any difference in I1 or J over 1024 offsets: %d\n", diff);
    // 3) the excess the pins demand, expressed in raw second-product units (pre >>18)
    printf("\nwhat a correction would have to be, in units of the SECOND product (pre >>18):\n");
    for(int t : {17,342,-342}){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; i64 P=arr(I1,dx)+K;
        printf("  t=%4d  I1=%8lld  dx=%9lld  P=%14lld  J=%lld\n", t,(long long)I1,(long long)dx,(long long)P,(long long)(P>>18)); }
    printf("  needed dJ: t=17 -13   t=342 -10   t=-342 +1..+10\n");
    printf("  as dP  :  t=17 %lld   t=342 %lld   t=-342 %lld..%lld\n",
        (long long)(-13ll<<18),(long long)(-10ll<<18),(long long)(1ll<<18),(long long)(10ll<<18));
    printf("  as dI1 (dP/dx): t=17 %.2f   t=342 %.2f   t=-342 %.2f..%.2f\n",
        (double)(-13ll<<18)/139264.0,(double)(-10ll<<18)/2801664.0,(double)(1ll<<18)/-2801664.0,(double)(10ll<<18)/-2801664.0);
    return 0; }
