// c12_dbg.cpp -- does the normalised latch actually deliver the (342/17)^2 ratio?  Measure the two quantities
// the lock is about, directly: the granularity of the square term at t=17 and the sign asymmetry at t=342,
// for a normalised chain, and compare with the fixed-alignment chain.
#include "base2.h"
#include <cstdio>
#include <set>
#include <algorithm>
static inline int nbits(i64 v){ if(v<0)v=-v; return v? 64-__builtin_clzll((u64)v):0; }
int main(){ build_tables();
    const i64 S=TB[16].S; const int u2=30;                 // node 16: V = C0 - T2, S = 2^23-1
    puts("NORMALISED chain:  P1 = arr(KC,dx)+2^19 -> m1 (W1 bits) ; P2 = arr(m1,dx)+2^19 -> m2 (W2 bits)");
    puts("                   T2 = sm(arr(m2,S)+2^19, 43+u2-n1-n2)");
    puts(" W1 W2 | t=17: T2 and its step per m2-LSB | t=342: T2, and T2(-342)-T2(342)");
    for(int W1=22;W1<=25;W1++) for(int W2=22;W2<=25;W2++){
        auto ev=[&](int t,i64 dm2,i64&T2,int&n1,int&n2){ i64 dx=(i64)t<<13;
            i64 P1=arr(KC,dx)+K; n1=nbits(P1)-W1; if(n1<0)n1=0; i64 m1=P1>>n1;
            i64 P2=arr(m1,dx)+K; n2=nbits(P2)-W2; if(n2<0)n2=0; i64 m2=(P2>>n2)+dm2;
            int u=43+u2-n1-n2; if(u<0||u>62){T2=-1;return;} T2=sm(arr(m2,S)+K,u); };
        i64 a,b; int n1,n2;
        ev(17,0,a,n1,n2); ev(17,1,b,n1,n2); i64 step17=b-a;
        i64 p,m; ev(342,0,p,n1,n2); ev(-342,0,m,n1,n2);
        printf(" %2d %2d | T2(17)=%-8lld step=%-6lld | T2(342)=%-10lld  D=%lld\n",
            W1,W2,(long long)a,(long long)step17,(long long)p,(long long)(m-p)); }
    puts("\nFIXED-alignment chain for comparison (s1=24, s2=17, the only shift the lock allows):");
    { auto ev=[&](int t,i64 dJ,i64&T2){ i64 dx=(i64)t<<13; i64 L=(arr(KC,dx)+K)>>24; i64 J=((arr(L,dx)+K)>>17)+dJ;
          T2=sm(arr(J,S)+K,u2+3); };
      i64 a,b,p,m; ev(17,0,a); ev(17,1,b); ev(342,0,p); ev(-342,0,m);
      printf("        T2(17)=%-8lld step per J-LSB=%-6lld | T2(342)=%-10lld  D=%lld\n",
        (long long)a,(long long)(b-a),(long long)p,(long long)(m-p)); }
    return 0; }
