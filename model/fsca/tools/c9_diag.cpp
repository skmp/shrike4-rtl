// c9_diag.cpp -- can the two-ladder family reach J(17) = 45617 at all?  Scan the band directly and print the
// achievable values, to check the analytic loss argument against the actual array arithmetic.
#include "base2.h"
#include <cstdio>
#include <algorithm>
#include <set>
int main(){ build_tables();
    const i64 TARGET=45617; const i64 IDEAL_NUM=(i64)KC*17*17;   // /2^15
    printf("t=17: target J = %lld, exact = %.3f\n",(long long)TARGET,(double)IDEAL_NUM/32768.0);
    for(int s3=9;s3<=13;s3++){ int s1=24,s2=18+ (0);
        // choose s2 so C2 is a sensible field:  m = s1+s2+s3-41
        for(s2=16;s2<=28;s2++){ int m=s1+s2+s3-41; if(m<0||m>46) continue;
            i64 sh1=1ll<<(s1-13), sh2=1ll<<(s2-13);
            // report the range of J(17) reachable as C1 sweeps its band
            i64 jmin=1ll<<60, jmax=-1; int hit=0; std::set<i64> vals;
            for(i64 C1=(i64)(8.0*sh1); C1<=(i64)(300.0*sh1); C1++){
                __int128 num=(__int128)KC<<m; i64 C2=(i64)(num/C1);
                if(C2<(1ll<<17)||C2>=(1ll<<26)) continue;
                for(i64 d=-2;d<=2;d++){ i64 c2=C2+d;
                  for(int k1=0;k1<2;k1++) for(int k2=0;k2<2;k2++) for(int k3=0;k3<2;k3++){
                    i64 K1=k1?(1ll<<19):0,K2=k2?(1ll<<19):0,K3=k3?(1ll<<19):0;
                    i64 dx=(i64)17<<13; i64 L1=(arr(C1,dx)+K1)>>s1, L2=(arr(c2,dx)+K2)>>s2;
                    i64 J=(arr(L1,L2)+K3)>>s3;
                    if(J<jmin)jmin=J; if(J>jmax)jmax=J; if(J==TARGET)hit++;
                    if(vals.size()<400000) vals.insert(J); } } }
            if(jmax<0) continue;
            printf("  s1=%d s2=%2d s3=%2d : J(17) ranges [%lld, %lld], %zu distinct, target reached %d times%s\n",
                s1,s2,s3,(long long)jmin,(long long)jmax,vals.size(),hit, hit?"  <== REACHABLE":"");
        } }
    return 0; }
