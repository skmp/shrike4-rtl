// wa_arr.cpp -- measure the exact deviation of arr(a,b) from the true product a*b for the operand
// shapes the square chain uses.  This bounds every "truncation" degree of freedom in the family.
#include "wa_common.h"
int main(){ build_tables();
    puts("first multiply: arr(cst, t<<g) - cst*(t<<g)   [both roles]");
    for(int g=8;g<=15;g++){ i64 lo0=1ll<<60,hi0=-(1ll<<60),lo1=1ll<<60,hi1=-(1ll<<60);
        for(int t=-512;t<=511;t++){ i64 d=(i64)t<<g; i64 e0=arr(KC,d)-KC*d, e1=arr(d,KC)-KC*d;
            lo0=std::min(lo0,e0);hi0=std::max(hi0,e0);lo1=std::min(lo1,e1);hi1=std::max(hi1,e1); }
        printf("  g=%2d  arr(KC,dx): [%9lld,%9lld] = [%.3f,%.3f]*2^16   arr(dx,KC): [%9lld,%9lld] = [%.3f,%.3f]*2^16\n",
            g,(long long)lo0,(long long)hi0,lo0/65536.0,hi0/65536.0,(long long)lo1,(long long)hi1,lo1/65536.0,hi1/65536.0); }
    puts("second multiply: arr(I1, t<<g) - I1*(t<<g), I1 = round(KC*t*2^-(17+e)) for e=-8..-3");
    for(int e=-8;e<=-3;e++) for(int g=8;g<=15;g++){ i64 lo0=1ll<<60,hi0=-(1ll<<60),lo1=1ll<<60,hi1=-(1ll<<60);
        for(int t=-512;t<=511;t++){ i64 I1=(i64)llround((long double)KC*t/powl(2.0L,17+e)); i64 d=(i64)t<<g;
            i64 e0=arr(I1,d)-I1*d, e1=arr(d,I1)-I1*d;
            lo0=std::min(lo0,e0);hi0=std::max(hi0,e0);lo1=std::min(lo1,e1);hi1=std::max(hi1,e1); }
        printf("  e=%d g=%2d arr(I1,dx): [%.3f,%.3f]*2^16   arr(dx,I1): [%.3f,%.3f]*2^16\n",
            e,g,lo0/65536.0,hi0/65536.0,lo1/65536.0,hi1/65536.0); }
    return 0; }
