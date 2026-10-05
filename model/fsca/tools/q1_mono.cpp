// q1_mono.cpp -- the workflow reports arr() is NOT monotone in its RECODED operand beyond 2^27
// (14 Booth digits = bits 0..26), but IS monotone in the MULTIPLICAND to 2^29.  My p2/p3/p4 binary
// searches solved the per-node coefficient as the MULTIPLICAND, so check that specific claim.
#include "base2.h"
#include <cstdio>
int main(){
    // rec = sq (a square, <= 2^18 here); mul = C2 searched to 2^28. Check monotonicity in mul.
    long long bad=0, n=0; i64 worst=0;
    for(i64 sq : {(i64)1,(i64)7,(i64)1000,(i64)65536,(i64)262143,(i64)262144}){
        i64 prev=arr(sq,0);
        for(i64 c=1;c<(1LL<<28);c+= (c<4096?1:4093)){ i64 v=arr(sq,c); n++;
            if(v<prev){ bad++; if(prev-v>worst) worst=prev-v; } prev=v; } }
    printf("monotone in MULTIPLICAND over %lld steps up to 2^28: %s (backward steps %lld, worst %lld)\n",
        n, bad?"NO":"YES", bad, (long long)worst);
    // and the recoded-operand limit the workflow found
    bad=0;n=0;worst=0; i64 mul=6974873, prev=arr(0,mul);
    i64 firstbad=0;
    for(i64 v=1;v<(1LL<<28);v+=1){ i64 x=arr(v,mul); n++;
        if(x<prev){ bad++; if(!firstbad)firstbad=v; if(prev-x>worst)worst=prev-x; } prev=x;
        if(v>(1LL<<27)+4096) break; }
    printf("monotone in RECODED operand: first backward step at v = %lld (2^27 = %lld), worst %lld\n",
        (long long)firstbad,(long long)(1LL<<27),(long long)worst);
    // the values my p2/p3/p4 actually used as rec: sq = (arr(dx,dx)+K)>>26 ~ t^2, and J
    i64 mx=0; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 s=(arr(dx,dx)+K)>>26; if(s>mx)mx=s;
        i64 I1=(arr(KC,dx)+K)>>23, J=(arr(I1,dx)+K)>>20; if(J>mx)mx=J; }
    printf("max recoded operand actually used by p2/p3/p4: %lld  (limit 2^27 = %lld) -> %s\n",
        (long long)mx,(long long)(1LL<<27), mx<(1LL<<27)?"SAFE":"OUT OF RANGE");
    return 0; }
