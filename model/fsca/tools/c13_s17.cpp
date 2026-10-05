// c13_s17.cpp -- check the one shift pair the tension singles out, concretely, at the three pinned offsets.
#include "base2.h"
#include <cstdio>
int main(){ build_tables();
    const i64 W17=45617, W342=18466836, Wm342lo=18466858, Wm342hi=18466867;
    printf("windows: J(17)=%lld  J(342)=%lld  J(-342) in [%lld,%lld]\n\n",
        (long long)W17,(long long)W342,(long long)Wm342lo,(long long)Wm342hi);
    puts(" s1 s2 K1     K2     |   J(17)   dev  |  J(342)   dev  |  J(-342)  dev  |  D   | hits");
    for(int s1=22;s1<=26;s1++){ int s2=41-s1;
      for(int k1=0;k1<2;k1++) for(int k2=0;k2<2;k2++){
        i64 K1=k1?(1ll<<19):0, K2=k2?(1ll<<19):0;
        auto J=[&](int t){ i64 dx=(i64)t<<13; i64 L=(arr(KC,dx)+K1)>>s1; return (arr(L,dx)+K2)>>s2; };
        i64 a=J(17), b=J(342), c=J(-342);
        int h=(a==W17)+(b==W342)+(c>=Wm342lo&&c<=Wm342hi);
        printf(" %2d %2d %-6lld %-6lld | %8lld %+5lld | %8lld %+5lld | %8lld %+5lld | %3lld  | %d%s\n",
            s1,s2,(long long)K1,(long long)K2,(long long)a,(long long)(a-W17),(long long)b,(long long)(b-W342),
            (long long)c,(long long)(c-Wm342lo),(long long)(c-b),h, h==3?"  <== ALL THREE":""); } }
    puts("\nsame, scanning the constant around KC at s1=24,s2=17 (K1=K2=2^19):");
    for(i64 d=-6;d<=6;d++){ i64 C=KC+d;
        auto J=[&](int t){ i64 dx=(i64)t<<13; i64 L=(arr(C,dx)+K)>>24; return (arr(L,dx)+K)>>17; };
        i64 a=J(17),b=J(342),c=J(-342);
        printf("  KC%+lld : J(17)=%lld (%+lld)  J(342)=%lld (%+lld)  D=%lld\n",
            (long long)d,(long long)a,(long long)(a-W17),(long long)b,(long long)(b-W342),(long long)(c-b)); }
    return 0; }
